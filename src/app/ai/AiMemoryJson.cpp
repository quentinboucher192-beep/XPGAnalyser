// =============================================================================
//  app/ai/AiMemoryJson.cpp - le JSON des fichiers de l'IA (voir AiMemoryJson.hpp)
// =============================================================================
#include "AiMemoryJson.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace app::ai::memory {

// ------------------------------------------------------------------ la valeur ---
Json Json::makeString(std::string s) {
    Json j;
    j.type = Type::String;
    j.text = std::move(s);
    return j;
}

Json Json::makeNumber(double v) {
    Json j;
    j.type = Type::Number;
    j.number = v;
    return j;
}

Json Json::makeBool(bool v) {
    Json j;
    j.type = Type::Bool;
    j.boolean = v;
    return j;
}

Json Json::makeObject() {
    Json j;
    j.type = Type::Object;
    return j;
}

Json Json::makeArray() {
    Json j;
    j.type = Type::Array;
    return j;
}

const Json* Json::get(std::string_view key) const noexcept {
    if (type != Type::Object) return nullptr;
    for (const auto& [k, v] : fields)
        if (k == key) return &v;
    return nullptr;
}

Json* Json::get(std::string_view key) noexcept {
    if (type != Type::Object) return nullptr;
    for (auto& [k, v] : fields)
        if (k == key) return &v;
    return nullptr;
}

Json& Json::set(std::string key, Json value) {
    if (type != Type::Object) {
        *this = makeObject();
    }
    for (auto& [k, v] : fields)
        if (k == key) {
            v = std::move(value);
            return v;
        }
    fields.emplace_back(std::move(key), std::move(value));
    return fields.back().second;
}

bool Json::erase(std::string_view key) {
    if (type != Type::Object) return false;
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (it->first == key) {
            fields.erase(it);
            return true;
        }
    return false;
}

std::string Json::str(std::string_view key, std::string_view fallback) const {
    const Json* v = get(key);
    if (!v) return std::string(fallback);
    if (v->type == Type::String) return v->text;
    if (v->type == Type::Number) return v->text.empty() ? std::to_string(v->number) : v->text;
    return std::string(fallback);
}

double Json::num(std::string_view key, double fallback) const {
    const Json* v = get(key);
    if (!v) return fallback;
    if (v->type == Type::Number) return v->number;
    if (v->type == Type::String) {
        char* end = nullptr;
        const double d = std::strtod(v->text.c_str(), &end);
        if (end && end != v->text.c_str() && *end == '\0') return d;
    }
    return fallback;
}

bool Json::flag(std::string_view key, bool fallback) const {
    const Json* v = get(key);
    if (!v) return fallback;
    if (v->type == Type::Bool) return v->boolean;
    if (v->type == Type::Number) return v->number != 0.0;
    if (v->type == Type::String) return v->text == "true" || v->text == "1" || v->text == "oui";
    return fallback;
}

// ------------------------------------------------------------------ la lecture ---
namespace {

constexpr int kMaxDepth = 64;

struct Reader {
    std::string_view s;
    std::size_t      i{0};
    std::string      error;

    bool fail(const char* what) {
        if (error.empty()) error = "position " + std::to_string(i) + " : " + what;
        return false;
    }
    void blanks() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool literal(std::string_view word) {
        if (s.substr(i, word.size()) != word) return fail("mot inconnu");
        i += word.size();
        return true;
    }
    static void utf8(std::string& out, std::uint32_t cp) {
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
    bool hex4(std::uint32_t& out) {
        if (i + 4 > s.size()) return fail("\\u incomplet");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("\\u mal forme");
        }
        return true;
    }
    bool string(std::string& out) {
        ++i;   // le guillemet ouvrant
        while (i < s.size()) {
            const char c = s[i];
            if (c == '"') {
                ++i;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return fail("caractere de controle dans une chaine");
            if (c != '\\') {
                out += c;
                ++i;
                continue;
            }
            if (++i >= s.size()) break;
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
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        std::uint32_t low = 0;
                        if (!hex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        else {
                            utf8(out, 0xFFFD);
                            cp = low;
                        }
                    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("echappement inconnu");
            }
        }
        return fail("chaine non fermee");
    }
    bool number(Json& out) {
        const std::size_t start = i;
        if (s[i] == '-') ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        out.type = Json::Type::Number;
        out.text = std::string(s.substr(start, i - start));
        char* end = nullptr;
        out.number = std::strtod(out.text.c_str(), &end);
        if (!end || end == out.text.c_str() || *end != '\0') return fail("nombre mal forme");
        return true;
    }
    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("trop imbrique");
        blanks();
        if (i >= s.size()) return fail("valeur attendue");
        const char c = s[i];
        if (c == '{') {
            out = Json::makeObject();
            ++i;
            blanks();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            while (true) {
                blanks();
                if (i >= s.size() || s[i] != '"') return fail("cle attendue");
                std::string key;
                if (!string(key)) return false;
                blanks();
                if (i >= s.size() || s[i] != ':') return fail("':' attendu");
                ++i;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.fields.emplace_back(std::move(key), std::move(v));
                blanks();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return fail("',' ou '}' attendu");
            }
        }
        if (c == '[') {
            out = Json::makeArray();
            ++i;
            blanks();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            while (true) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                blanks();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return fail("',' ou ']' attendu");
            }
        }
        if (c == '"') {
            out.type = Json::Type::String;
            out.text.clear();
            return string(out.text);
        }
        if (c == 't') {
            out = Json::makeBool(true);
            return literal("true");
        }
        if (c == 'f') {
            out = Json::makeBool(false);
            return literal("false");
        }
        if (c == 'n') {
            out = Json{};
            return literal("null");
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail("valeur inattendue");
    }
};

void escaped(std::string& out, std::string_view s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void write(std::string& out, const Json& v, bool indent, int depth) {
    const auto newline = [&](int d) {
        if (!indent) return;
        out += '\n';
        out.append(static_cast<std::size_t>(d) * 2, ' ');
    };
    switch (v.type) {
        case Json::Type::Null: out += "null"; return;
        case Json::Type::Bool: out += v.boolean ? "true" : "false"; return;
        case Json::Type::Number: {
            if (!v.text.empty()) {
                out += v.text;
                return;
            }
            if (!std::isfinite(v.number)) {
                out += "null";
                return;
            }
            char buf[40];
            if (std::floor(v.number) == v.number && std::fabs(v.number) < 9.0e15)
                std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v.number));
            else
                std::snprintf(buf, sizeof buf, "%.17g", v.number);
            out += buf;
            return;
        }
        case Json::Type::String: escaped(out, v.text); return;
        case Json::Type::Array: {
            if (v.items.empty()) {
                out += "[]";
                return;
            }
            out += '[';
            for (std::size_t k = 0; k < v.items.size(); ++k) {
                if (k) out += indent ? "," : ", ";
                newline(depth + 1);
                write(out, v.items[k], indent, depth + 1);
            }
            newline(depth);
            out += ']';
            return;
        }
        case Json::Type::Object: {
            if (v.fields.empty()) {
                out += "{}";
                return;
            }
            out += '{';
            for (std::size_t k = 0; k < v.fields.size(); ++k) {
                if (k) out += indent ? "," : ", ";
                newline(depth + 1);
                escaped(out, v.fields[k].first);
                out += ": ";
                write(out, v.fields[k].second, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            return;
        }
    }
}

} // namespace

bool parseJson(std::string_view text, Json& out, std::string* why) {
    // Un BOM UTF-8 en tete (un fichier passe par le Bloc-notes) ne gene pas.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF)
        text.remove_prefix(3);
    Reader r{text, 0, {}};
    Json v;
    if (!r.value(v, 0)) {
        if (why) *why = r.error;
        return false;
    }
    r.blanks();
    if (r.i != text.size()) {
        if (why) *why = "position " + std::to_string(r.i) + " : texte apres la valeur";
        return false;
    }
    out = std::move(v);
    return true;
}

std::string writeJson(const Json& value, bool indent) {
    std::string out;
    write(out, value, indent, 0);
    if (indent) out += '\n';
    return out;
}

} // namespace app::ai::memory
