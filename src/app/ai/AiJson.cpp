// =============================================================================
//  app/ai/AiJson.cpp - voir AiJson.hpp
// =============================================================================
#include "AiJson.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app::ai::json {

namespace {
const std::string kEmpty;
const Value       kNull;
constexpr int     kMaxDepth = 200;

// ---- la lecture -------------------------------------------------------------
struct Reader {
    std::string_view s;
    std::size_t      i{0};
    std::string      error;

    void fail(const std::string& why) {
        if (!error.empty()) return;
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < i && k < s.size(); ++k) {
            if (s[k] == '\n') { ++line; col = 1; }
            else ++col;
        }
        error = "ligne " + std::to_string(line) + ", colonne " + std::to_string(col) + " : " + why;
    }
    void blanks() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    bool literal(std::string_view w) {
        if (s.substr(i, w.size()) != w) return false;
        i += w.size();
        return true;
    }
    static void putUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& out) {
        if (i + 4 > s.size()) return false;
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i + static_cast<std::size_t>(k)];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        i += 4;
        return true;
    }
    bool string(std::string& out) {
        ++i;   // le guillemet
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) { --i; fail("caract\xC3\xA8re de contr\xC3\xB4le dans une cha\xC3\xAEne"); return false; }
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) { fail("\\u mal form\xC3\xA9"); return false; }
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) { fail("paire \\u mal form\xC3\xA9" "e"); return false; }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default:
                    --i;
                    fail("\xC3\xA9" "chappement inconnu");
                    return false;
            }
        }
        fail("cha\xC3\xAEne non termin\xC3\xA9" "e");
        return false;
    }
    bool number(Value& out) {
        const std::size_t from = i;
        if (i < s.size() && s[i] == '-') ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        const std::string text(s.substr(from, i - from));
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (text.empty() || !end || *end != '\0') { i = from; fail("nombre illisible"); return false; }
        out = Value(d);
        return true;
    }
    bool value(Value& out, int depth) {
        if (depth > kMaxDepth) { fail("trop imbriqu\xC3\xA9"); return false; }
        blanks();
        if (i >= s.size()) { fail("valeur attendue"); return false; }
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Value::object();
            blanks();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            while (true) {
                blanks();
                if (i >= s.size() || s[i] != '"') { fail("cl\xC3\xA9 attendue"); return false; }
                std::string key;
                if (!string(key)) return false;
                blanks();
                if (i >= s.size() || s[i] != ':') { fail("':' attendu"); return false; }
                ++i;
                Value v;
                if (!value(v, depth + 1)) return false;
                out.members().emplace_back(std::move(key), std::move(v));
                blanks();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                fail("',' ou '}' attendu");
                return false;
            }
        }
        if (c == '[') {
            ++i;
            out = Value::array();
            blanks();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            while (true) {
                Value v;
                if (!value(v, depth + 1)) return false;
                out.items().push_back(std::move(v));
                blanks();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                fail("',' ou ']' attendu");
                return false;
            }
        }
        if (c == '"') {
            std::string text;
            if (!string(text)) return false;
            out = Value(std::move(text));
            return true;
        }
        if (literal("true")) { out = Value(true); return true; }
        if (literal("false")) { out = Value(false); return true; }
        if (literal("null")) { out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        fail("valeur inattendue");
        return false;
    }
};

// ---- l'ecriture ---------------------------------------------------------------
void quote(std::string& out, const std::string& text) {
    out += '"';
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    out += '"';
}

void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "null"; return; }
    if (std::fabs(d) < 9.0e15 && d == std::floor(d)) {
        out += std::to_string(static_cast<long long>(d));
        return;
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", d);
    out += buf;
}

void writeValue(std::string& out, const Value& v, bool indent, int depth) {
    const auto newline = [&](int d) {
        if (!indent) return;
        out += '\n';
        out.append(static_cast<std::size_t>(d) * 2, ' ');
    };
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: writeNumber(out, v.asNumber()); break;
        case Value::Type::String: quote(out, v.asString()); break;
        case Value::Type::Array: {
            if (v.items().empty()) { out += "[]"; break; }
            // Un tableau de scalaires tient sur une ligne ; d'objets, un par ligne.
            bool flat = true;
            for (const auto& x : v.items())
                if (x.isObject() || x.isArray()) flat = false;
            out += '[';
            bool first = true;
            for (const auto& x : v.items()) {
                if (!first) out += flat && indent ? ", " : ",";
                first = false;
                if (!flat) newline(depth + 1);
                writeValue(out, x, indent, depth + 1);
            }
            if (!flat) newline(depth);
            out += ']';
            break;
        }
        case Value::Type::Object: {
            if (v.members().empty()) { out += "{}"; break; }
            out += '{';
            bool first = true;
            for (const auto& [k, x] : v.members()) {
                if (!first) out += ',';
                first = false;
                newline(depth + 1);
                quote(out, k);
                out += indent ? ": " : ":";
                writeValue(out, x, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            break;
        }
    }
}
} // namespace

bool Value::asBool(bool fallback) const noexcept { return type_ == Type::Bool ? bool_ : fallback; }
double Value::asNumber(double fallback) const noexcept { return type_ == Type::Number ? number_ : fallback; }
std::int64_t Value::asInt(std::int64_t fallback) const noexcept {
    return type_ == Type::Number ? static_cast<std::int64_t>(std::llround(number_)) : fallback;
}
const std::string& Value::asString() const noexcept { return type_ == Type::String ? text_ : kEmpty; }

void Value::push(Value v) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ == Type::Array) items_.push_back(std::move(v));
}

const Value* Value::find(std::string_view key) const noexcept {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}
Value* Value::find(std::string_view key) noexcept {
    if (type_ != Type::Object) return nullptr;
    for (auto& m : members_)
        if (m.first == key) return &m.second;
    return nullptr;
}
const Value& Value::at(std::string_view key) const noexcept {
    const Value* v = find(key);
    return v ? *v : kNull;
}
Value& Value::operator[](std::string_view key) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (Value* v = find(key)) return *v;
    members_.emplace_back(std::string(key), Value());
    return members_.back().second;
}
bool Value::erase(std::string_view key) {
    for (auto it = members_.begin(); it != members_.end(); ++it)
        if (it->first == key) {
            members_.erase(it);
            return true;
        }
    return false;
}
std::string Value::str(std::string_view key, std::string_view fallback) const {
    const Value* v = find(key);
    return v && v->isString() ? v->asString() : std::string(fallback);
}
double Value::num(std::string_view key, double fallback) const noexcept {
    const Value* v = find(key);
    return v ? v->asNumber(fallback) : fallback;
}
std::int64_t Value::integer(std::string_view key, std::int64_t fallback) const noexcept {
    const Value* v = find(key);
    return v ? v->asInt(fallback) : fallback;
}
bool Value::flag(std::string_view key, bool fallback) const noexcept {
    const Value* v = find(key);
    return v ? v->asBool(fallback) : fallback;
}

bool Value::operator==(const Value& o) const noexcept {
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return bool_ == o.bool_;
        case Type::Number: return number_ == o.number_;
        case Type::String: return text_ == o.text_;
        case Type::Array: return items_ == o.items_;
        case Type::Object: return members_ == o.members_;
    }
    return false;
}

bool parse(std::string_view text, Value& out, std::string* error) {
    Reader r;
    r.s = text;
    // Un BOM UTF-8 en tete (un fichier retouche sous Windows) : passe.
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") r.i = 3;
    Value v;
    if (!r.value(v, 0)) {
        if (error) *error = r.error;
        return false;
    }
    r.blanks();
    if (r.i != text.size()) {
        r.fail("texte apr\xC3\xA8s la valeur");
        if (error) *error = r.error;
        return false;
    }
    out = std::move(v);
    return true;
}

std::string write(const Value& v, bool indent) {
    std::string out;
    writeValue(out, v, indent, 0);
    if (indent) out += '\n';
    return out;
}

Value unknownMembers(const Value& object, std::initializer_list<std::string_view> known) {
    Value extra = Value::object();
    for (const auto& [k, v] : object.members()) {
        bool isKnown = false;
        for (const auto kk : known)
            if (kk == k) { isKnown = true; break; }
        if (!isKnown) extra.members().emplace_back(k, v);
    }
    return extra;
}

void addMembers(Value& object, const Value& extra) {
    for (const auto& [k, v] : extra.members())
        if (!object.has(k)) object.members().emplace_back(k, v);
}

} // namespace app::ai::json
