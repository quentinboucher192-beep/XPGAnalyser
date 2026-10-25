// =============================================================================
//  sim/Json.cpp - 1.13.0 : lire et ecrire du JSON (voir l'en-tete)
// =============================================================================
#include "Json.hpp"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sim {

namespace {

constexpr int kMaxDepth = 200;             // un JSON trop profond : refuse (au lieu de saturer la pile)

// Une boite ANY qui porte `payload` (une valeur concrete) ; nullptr : JSON null.
ObjRef anyBox(ObjRef payload) {
    auto box = makeObj(anyType());         // bound = true (present) par defaut
    if (payload) box->items.push_back(std::move(payload));
    return box;
}
ObjRef scalarBox(Value v) {
    auto s = std::make_shared<Obj>();
    s->type = scalarType(v.type());
    s->value = std::move(v);
    return anyBox(std::move(s));
}

// ---- l'analyseur -------------------------------------------------------------
struct Reader {
    std::string_view s;
    std::size_t      i{0};
    JsonStatus*      status{nullptr};

    bool fail(std::string what) {
        if (status->ok) {
            status->ok = false;
            status->message = "JSON mal form\xC3\xA9 (position " + std::to_string(i) + ") : " + std::move(what);
        }
        return false;
    }
    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool atEnd() { return i >= s.size(); }

    // Un point de code en UTF-8 (jusqu'a 4 octets).
    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
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
        if (i + 4 > s.size()) return fail("\\u attend quatre chiffres hexad\xC3\xA9" "cimaux");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("\\u attend quatre chiffres hexad\xC3\xA9" "cimaux");
        }
        return true;
    }
    // Lit une chaine (apres le guillemet ouvrant) dans `out` ; faux (status) si mal formee.
    bool readString(std::string& out) {
        for (;;) {
            if (atEnd()) return fail("une cha\xC3\xAEne n'est pas ferm\xC3\xA9" "e (il manque \")");
            const unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"') return true;
            if (c == '\\') {
                if (atEnd()) return fail("un \\ final sans caract\xC3\xA8re \xC3\xA0 \xC3\xA9" "chapper");
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
                        if (cp >= 0xD800 && cp <= 0xDBFF) {                  // un couple de substitution
                            if (i + 2 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                                i += 2;
                                std::uint32_t lo = 0;
                                if (!hex4(lo)) return false;
                                if (lo < 0xDC00 || lo > 0xDFFF) return fail("couple \\u invalide");
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                return fail("un demi-couple \\u sans sa suite");
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: return fail(std::string("\\") + e + " n'est pas une \xC3\xA9" "chapp\xC3\xA9" "e JSON");
                }
            } else if (c < 0x20) {
                return fail("un caract\xC3\xA8re de contr\xC3\xB4le brut dans une cha\xC3\xAEne (\\n, \\t\xE2\x80\xA6)");
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    ObjRef readNumber() {
        const std::size_t start = i;
        if (i < s.size() && s[i] == '-') ++i;
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) { fail("un nombre attendu"); return nullptr; }
        bool integral = true;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '.') {
            integral = false;
            ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) { fail("un chiffre attendu apr\xC3\xA8s le point"); return nullptr; }
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            integral = false;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) { fail("un chiffre attendu dans l'exposant"); return nullptr; }
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        const std::string token(s.substr(start, i - start));
        if (integral) {
            errno = 0;
            char* end = nullptr;
            const long long v = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end == token.c_str() + token.size())
                return scalarBox(Value::integer(Type::LInt, static_cast<std::int64_t>(v)));
            // trop grand pour un LINT : on garde un LREAL (precision moindre, mais lisible)
        }
        return scalarBox(Value::real(std::strtod(token.c_str(), nullptr)));
    }
    bool literal(std::string_view word) {
        if (s.size() - i >= word.size() && s.compare(i, word.size(), word) == 0) { i += word.size(); return true; }
        return false;
    }
    // Rend une boite ANY (la valeur JSON). nullptr + status si erreur.
    ObjRef value(int depth) {
        if (depth > kMaxDepth) { fail("JSON trop imbriqu\xC3\xA9 (plus de " + std::to_string(kMaxDepth) + " niveaux)"); return nullptr; }
        skipWs();
        if (atEnd()) { fail("une valeur attendue, le texte s'arr\xC3\xAAte"); return nullptr; }
        const char c = s[i];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') {
            ++i;
            std::string str;
            if (!readString(str)) return nullptr;
            return scalarBox(Value::text(std::move(str)));
        }
        if (c == 't') { if (literal("true")) return scalarBox(Value::boolean(true)); fail("valeur inconnue"); return nullptr; }
        if (c == 'f') { if (literal("false")) return scalarBox(Value::boolean(false)); fail("valeur inconnue"); return nullptr; }
        if (c == 'n') { if (literal("null")) return makeObj(anyType()); fail("valeur inconnue"); return nullptr; }
        if (c == '-' || (c >= '0' && c <= '9')) return readNumber();
        fail(std::string("caract\xC3\xA8re inattendu '") + c + "'");
        return nullptr;
    }
    ObjRef object(int depth) {
        ++i;                                   // le {
        auto map = makeObj(mapAnyType());      // MAP[STRING] OF ANY
        skipWs();
        if (!atEnd() && s[i] == '}') { ++i; return anyBox(map); }
        for (;;) {
            skipWs();
            if (atEnd() || s[i] != '"') { fail("une cl\xC3\xA9 entre guillemets attendue"); return nullptr; }
            ++i;
            std::string key;
            if (!readString(key)) return nullptr;
            skipWs();
            if (atEnd() || s[i] != ':') { fail("':' attendu apr\xC3\xA8s une cl\xC3\xA9"); return nullptr; }
            ++i;
            auto v = value(depth + 1);
            if (!v) return nullptr;
            map->textKeys[key] = std::move(v);
            skipWs();
            if (atEnd()) { fail("l'objet n'est pas ferm\xC3\xA9 (il manque })"); return nullptr; }
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; return anyBox(map); }
            fail("',' ou '}' attendu dans l'objet");
            return nullptr;
        }
    }
    ObjRef array(int depth) {
        ++i;                                   // le [
        auto list = makeObj(listType(anyType()));   // LIST OF ANY
        skipWs();
        if (!atEnd() && s[i] == ']') { ++i; return anyBox(list); }
        for (;;) {
            auto v = value(depth + 1);
            if (!v) return nullptr;
            list->items.push_back(std::move(v));
            skipWs();
            if (atEnd()) { fail("le tableau n'est pas ferm\xC3\xA9 (il manque ])"); return nullptr; }
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; return anyBox(list); }
            fail("',' ou ']' attendu dans le tableau");
            return nullptr;
        }
    }

    // 1.13.0 : MAP[STRING] OF ANY, le type d'un objet JSON.
    static TypeRef mapAnyType() {
        auto d = std::make_shared<TypeDesc>();
        d->kind = TypeDesc::Kind::Map;
        d->key = Type::String;
        d->element = anyType();
        return d;
    }
};

// ---- le serialiseur ----------------------------------------------------------
struct Writer {
    bool         indent{false};
    std::string  out;
    JsonStatus*  status{nullptr};

    bool refuse(std::string what) {
        if (status->ok) {
            status->ok = false;
            status->message = "cette valeur ne se met pas en JSON : " + std::move(what);
        }
        return false;
    }
    void newline(int level) {
        if (!indent) return;
        out += '\n';
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    }
    void quote(const std::string& str) {
        out += '"';
        for (const unsigned char c : str) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                        out += buf;
                    } else {
                        out += static_cast<char>(c);     // UTF-8 tel quel (lisible)
                    }
            }
        }
        out += '"';
    }
    void number(const Value& v) {
        if (v.type() == Type::Real) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", v.asReal());
            // le plus court qui se relit a l'identique (comme String(x) en JavaScript)
            for (int p = 1; p < 17; ++p) {
                char cand[64];
                std::snprintf(cand, sizeof cand, "%.*g", p, v.asReal());
                if (std::strtod(cand, nullptr) == v.asReal()) { std::strcpy(buf, cand); break; }
            }
            std::string n = buf;
            out += n;
            return;
        }
        if (v.type() == Type::ULInt || v.type() == Type::LWord) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%llu", static_cast<unsigned long long>(v.asInteger()));
            out += buf;
            return;
        }
        out += std::to_string(v.asInteger());
    }
    bool scalar(const Value& v) {
        switch (v.type()) {
            case Type::Bool: out += v.isTruthy() ? "true" : "false"; return true;
            case Type::String: quote(v.asString()); return true;
            case Type::Real:
                if (!std::isfinite(v.asReal())) return refuse("le JSON ne conna\xC3\xAEt ni l'infini ni NAN");
                number(v);
                return true;
            default:
                if (isNumeric(v.type()) && v.type() != Type::Time && !isDateType(v.type())) { number(v); return true; }
                quote(v.display());             // TIME, DATE, TOD, DT : un texte lisible
                return true;
        }
    }
    bool write(const Obj& o, int level) {
        if (!o.type) return refuse("une valeur sans type");
        switch (o.type->kind) {
            case TypeDesc::Kind::Scalar: return scalar(o.value);
            case TypeDesc::Kind::Any: {
                if (o.items.empty() || !o.items.front()) { out += "null"; return true; }
                return write(*o.items.front(), level);
            }
            case TypeDesc::Kind::List:
            case TypeDesc::Kind::Array: {
                if (o.items.empty()) { out += "[]"; return true; }
                out += '[';
                for (std::size_t k = 0; k < o.items.size(); ++k) {
                    if (k) out += ',';
                    newline(level + 1);
                    if (!o.items[k] || !write(*o.items[k], level + 1)) return false;
                }
                newline(level);
                out += ']';
                return true;
            }
            case TypeDesc::Kind::Tuple: {
                out += '[';
                for (std::size_t k = 0; k < o.items.size(); ++k) {
                    if (k) out += ',';
                    newline(level + 1);
                    if (!o.items[k] || !write(*o.items[k], level + 1)) return false;
                }
                newline(level);
                out += ']';
                return true;
            }
            case TypeDesc::Kind::Map: {
                const std::size_t count = o.textKeys.size() + o.intKeys.size();
                if (count == 0) { out += "{}"; return true; }
                out += '{';
                std::size_t k = 0;
                for (const auto& [key, val] : o.textKeys) {
                    if (k++) out += ',';
                    newline(level + 1);
                    quote(key);
                    out += indent ? ": " : ":";
                    if (!val || !write(*val, level + 1)) return false;
                }
                for (const auto& [key, val] : o.intKeys) {
                    if (k++) out += ',';
                    newline(level + 1);
                    quote(std::to_string(key));
                    out += indent ? ": " : ":";
                    if (!val || !write(*val, level + 1)) return false;
                }
                newline(level);
                out += '}';
                return true;
            }
            case TypeDesc::Kind::Struct: {
                if (o.type->members.empty()) { out += "{}"; return true; }
                out += '{';
                for (std::size_t k = 0; k < o.type->members.size() && k < o.items.size(); ++k) {
                    if (k) out += ',';
                    newline(level + 1);
                    quote(o.type->members[k].first);
                    out += indent ? ": " : ":";
                    if (!o.items[k] || !write(*o.items[k], level + 1)) return false;
                }
                newline(level);
                out += '}';
                return true;
            }
            case TypeDesc::Kind::Ref:
            case TypeDesc::Kind::Pointer: return refuse("une r\xC3\xA9" "f\xC3\xA9rence ou un pointeur (le JSON ne garde que des valeurs)");
            case TypeDesc::Kind::Callback: return refuse("un rappel (une fonction) ne se met pas en JSON");
            case TypeDesc::Kind::Iterator: return refuse("un it\xC3\xA9rateur ne se met pas en JSON");
        }
        return refuse("un genre inconnu");
    }
};

} // namespace

ObjRef parseJson(std::string_view text, JsonStatus& status) {
    status = JsonStatus{};
    Reader r{text, 0, &status};
    auto box = r.value(0);
    if (!box) return nullptr;
    r.skipWs();
    if (!r.atEnd()) { r.fail("du texte en trop apr\xC3\xA8s la valeur JSON"); return nullptr; }
    // Un objet / un tableau : rendu nu (MAP / LIST), pour s'affecter a MAP[STRING] OF ANY, LIST OF ANY.
    if (!box->items.empty() && box->items.front() && box->items.front()->type) {
        const auto k = box->items.front()->type->kind;
        if (k == TypeDesc::Kind::Map || k == TypeDesc::Kind::List) return box->items.front();
    }
    return box;
}

bool writeJson(const Obj& value, bool indent, std::string& out, JsonStatus& status) {
    status = JsonStatus{};
    Writer w;
    w.indent = indent;
    w.status = &status;
    if (!w.write(value, 0)) return false;
    out = std::move(w.out);
    return true;
}

} // namespace sim
