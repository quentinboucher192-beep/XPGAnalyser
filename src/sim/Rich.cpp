#include "Rich.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sim {

namespace {
std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
} // namespace

std::string TypeDesc::text() const {
    switch (kind) {
        case Kind::Scalar: return name.empty() ? std::string(toString(scalar)) : name;
        case Kind::Array: {
            std::string s = "ARRAY[";
            for (std::size_t d = 0; d < bounds.size(); ++d)
                s += (d ? ", " : "") + std::to_string(bounds[d].first) + ".." + std::to_string(bounds[d].second);
            return s + "] OF " + (element ? element->text() : std::string("?"));
        }
        case Kind::Struct: return name;
        case Kind::Map:
            return "MAP[" + (name.empty() ? std::string(toString(key)) : name) + "] OF " + (element ? element->text() : std::string("?"));
        case Kind::Ref: return "REF_TO " + (element ? element->text() : std::string("?"));
        case Kind::Pointer: return "POINTER TO " + (element ? element->text() : std::string("?"));
        case Kind::Iterator: return "MAP_ITERATOR";
        case Kind::List:
            if (name == "RING") return "RING[" + std::to_string(capacity) + "] OF " + (element ? element->text() : std::string("?"));
            return (name.empty() ? std::string("LIST") : name) + " OF " + (element ? element->text() : std::string("?"));
        case Kind::Tuple: {
            std::string s = "TUPLE(";
            for (std::size_t i = 0; i < members.size(); ++i)
                s += (i ? ", " : "") + (members[i].second ? members[i].second->text() : std::string("?"));
            return s + ")";
        }
        case Kind::Callback: {
            if (!name.empty()) return name;                       // un type rappel nomme (T_Filtre)
            std::string s = "CALLBACK (";
            for (std::size_t i = 0; i < members.size(); ++i) {
                const std::string t = members[i].second ? members[i].second->text() : std::string("?");
                s += (i ? "; " : "") + (members[i].first.empty() ? t : members[i].first + " : " + t);
            }
            s += ")";
            if (element) s += " : " + element->text();
            return s;
        }
    }
    return "?";
}

TypeRef callbackType(std::vector<std::pair<std::string, TypeRef>> params, TypeRef result) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::Callback;
    d->members = std::move(params);
    d->element = std::move(result);
    return d;
}

namespace {
bool numericScalar(const TypeDesc& t) noexcept {
    return t.kind == TypeDesc::Kind::Scalar && (t.scalar == Type::Real || isInteger(t.scalar));
}
bool fitsParam(const TypeRef& a, const TypeRef& b) {
    if (!a || !b) return true;                                   // inconnu d'un cote : on ne refuse pas
    if (numericScalar(*a) && numericScalar(*b)) return true;
    if (a->kind == TypeDesc::Kind::Struct && b->kind == TypeDesc::Kind::Scalar) return true;   // une enumeration (un DINT)
    if (a->kind == TypeDesc::Kind::Scalar && b->kind == TypeDesc::Kind::Struct) return true;
    return sameType(*a, *b);
}
} // namespace

bool callbackFits(const TypeDesc& want, const TypeDesc& got, std::string* why) {
    const auto signature = [](const TypeDesc& t) {
        TypeDesc copy = t;
        copy.name.clear();
        return copy.text();
    };
    const auto refuse = [&](const std::string& what) {
        if (why) *why = what + " : " + signature(want) + " attendu, " + signature(got) + " donn\xC3\xA9";
        return false;
    };
    if (want.kind != TypeDesc::Kind::Callback || got.kind != TypeDesc::Kind::Callback) return refuse("pas un rappel");
    if (want.members.size() != got.members.size())
        return refuse(std::to_string(got.members.size()) + " param\xC3\xA8tre" + (got.members.size() > 1 ? "s" : "") + " au lieu de "
                      + std::to_string(want.members.size()));
    for (std::size_t i = 0; i < want.members.size(); ++i)
        if (!fitsParam(want.members[i].second, got.members[i].second)) return refuse("le param\xC3\xA8tre " + std::to_string(i + 1) + " n'a pas le bon type");
    if (static_cast<bool>(want.element) != static_cast<bool>(got.element))
        return refuse(want.element ? "elle ne rend rien" : "elle rend une valeur que personne n'attend");
    if (want.element && !fitsParam(want.element, got.element)) return refuse("le retour n'a pas le bon type");
    return true;
}

bool TypeDesc::dynamic() const noexcept {
    switch (kind) {
        case Kind::Map:
        case Kind::List:
        case Kind::Callback: return true;      // 1.12.19 : un rappel vit dans la memoire de l'IHM
        case Kind::Array: return element && element->dynamic();
        case Kind::Struct:
        case Kind::Tuple:
            for (const auto& m : members)
                if (m.second && m.second->dynamic()) return true;
            return false;
        default: return false;
    }
}

TypeRef listType(TypeRef element, bool vector) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::List;
    d->name = vector ? "VECTOR" : "LIST";
    d->element = std::move(element);
    return d;
}

TypeRef setType(TypeRef element) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::List;
    d->name = "SET";
    d->element = std::move(element);
    return d;
}

TypeRef ringType(TypeRef element, std::int64_t capacity) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::List;
    d->name = "RING";
    d->capacity = capacity;
    d->element = std::move(element);
    return d;
}

bool isSetType(const TypeDesc& t) noexcept { return t.kind == TypeDesc::Kind::List && t.name == "SET"; }
bool isRingType(const TypeDesc& t) noexcept { return t.kind == TypeDesc::Kind::List && t.name == "RING"; }

std::size_t normalizeList(Obj& o) {
    if (!o.type || o.type->kind != TypeDesc::Kind::List) return 0;
    const std::size_t before = o.items.size();
    if (isRingType(*o.type)) {
        // Les plus anciennes (au debut) partent : restent les `capacity` dernieres.
        const auto keep = static_cast<std::size_t>(std::max<std::int64_t>(0, o.type->capacity));
        if (o.items.size() > keep) o.items.erase(o.items.begin(), o.items.begin() + static_cast<std::ptrdiff_t>(o.items.size() - keep));
        return before - o.items.size();
    }
    if (!isSetType(*o.type)) return 0;
    // Rangee (comme std::set : l'ordre des valeurs simples, celui de '<'), chaque valeur une fois.
    const auto less = [](const ObjRef& a, const ObjRef& b) {
        if (!a || !b || !a->type || !b->type) return static_cast<bool>(b) && !a;
        if (a->type->kind == TypeDesc::Kind::Scalar && b->type->kind == TypeDesc::Kind::Scalar) return a->value.compare(b->value) < 0;
        return false;
    };
    std::stable_sort(o.items.begin(), o.items.end(), less);
    std::vector<ObjRef> kept;
    kept.reserve(o.items.size());
    for (auto& i : o.items) {
        if (!kept.empty() && kept.back() && i && !less(kept.back(), i) && deepEquals(*kept.back(), *i)) continue;
        kept.push_back(std::move(i));
    }
    o.items = std::move(kept);
    return before - o.items.size();
}

TypeRef tupleType(std::vector<TypeRef> items) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::Tuple;
    for (std::size_t i = 0; i < items.size(); ++i) d->members.emplace_back("Item" + std::to_string(i + 1), std::move(items[i]));
    return d;
}

std::int64_t TypeDesc::count() const noexcept {
    if (kind != Kind::Array) return 1;
    std::int64_t n = 1;
    for (const auto& [lo, hi] : bounds) n *= std::max<std::int64_t>(0, hi - lo + 1);
    return n;
}

TypeRef scalarType(Type t, std::string_view name) {
    auto d = std::make_shared<TypeDesc>();
    d->kind = TypeDesc::Kind::Scalar;
    d->scalar = t;
    d->name = upperOf(name);
    return d;
}

bool sameType(const TypeDesc& a, const TypeDesc& b) noexcept {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case TypeDesc::Kind::Scalar: return a.scalar == b.scalar;
        case TypeDesc::Kind::Array:
            if (a.bounds.size() != b.bounds.size()) return false;
            for (std::size_t d = 0; d < a.bounds.size(); ++d)
                if (a.bounds[d].second - a.bounds[d].first != b.bounds[d].second - b.bounds[d].first) return false;
            return a.element && b.element && sameType(*a.element, *b.element);
        case TypeDesc::Kind::Struct: return upperOf(a.name) == upperOf(b.name);
        case TypeDesc::Kind::Map:
            return isInteger(a.key) == isInteger(b.key) && a.element && b.element && sameType(*a.element, *b.element);
        case TypeDesc::Kind::Ref:
        case TypeDesc::Kind::Pointer:
            return a.element && b.element && sameType(*a.element, *b.element);
        case TypeDesc::Kind::Iterator: return true;
        // 1.12.2 : LIST et VECTOR s'echangent (meme element) ; un tuple : memes types, dans l'ordre.
        case TypeDesc::Kind::List: return a.element && b.element && sameType(*a.element, *b.element);
        case TypeDesc::Kind::Tuple:
            if (a.members.size() != b.members.size()) return false;
            for (std::size_t i = 0; i < a.members.size(); ++i)
                if (!a.members[i].second || !b.members[i].second || !sameType(*a.members[i].second, *b.members[i].second)) return false;
            return true;
        case TypeDesc::Kind::Callback: return callbackFits(a, b);      // 1.12.19 : la meme signature
    }
    return false;
}

ObjRef makeObj(const TypeRef& type) {
    auto o = std::make_shared<Obj>();
    o->type = type;
    if (!type) return o;
    switch (type->kind) {
        case TypeDesc::Kind::Scalar: o->value = Value::defaultOf(type->scalar); break;
        case TypeDesc::Kind::Array: {
            const auto n = type->count();
            o->items.reserve(static_cast<std::size_t>(std::max<std::int64_t>(0, n)));
            for (std::int64_t i = 0; i < n; ++i) o->items.push_back(makeObj(type->element));
            break;
        }
        case TypeDesc::Kind::Struct:
        case TypeDesc::Kind::Tuple:
            o->items.reserve(type->members.size());
            for (const auto& m : type->members) o->items.push_back(makeObj(m.second));
            break;
        default: break;                  // une liste nait vide
    }
    return o;
}

ObjRef deepCopy(const Obj& from) {
    auto o = std::make_shared<Obj>();
    o->type = from.type;
    o->value = from.value;
    o->items.reserve(from.items.size());
    for (const auto& i : from.items) o->items.push_back(i ? deepCopy(*i) : nullptr);
    for (const auto& [k, v] : from.textKeys) o->textKeys.emplace(k, v ? deepCopy(*v) : nullptr);
    for (const auto& [k, v] : from.intKeys) o->intKeys.emplace(k, v ? deepCopy(*v) : nullptr);
    o->bound = from.bound;
    o->target = from.target;
    o->targetName = from.targetName;
    o->map = from.map;
    o->textKey = from.textKey;
    o->intKey = from.intKey;
    o->atEnd = from.atEnd;
    o->callee = from.callee;              // 1.12.19 : un rappel vise la meme fonction (une LAMBDA : le meme code)
    o->lambda = from.lambda;
    o->lambdaWhere = from.lambdaWhere;
    return o;
}

bool assignObj(Obj& into, const Obj& from, std::string* why) {
    if (!into.type || !from.type) {
        if (why) *why = "valeur sans type";
        return false;
    }
    const auto& t = *into.type;
    if (t.kind == TypeDesc::Kind::Scalar) {
        if (from.type->kind != TypeDesc::Kind::Scalar) {
            if (why) *why = "un " + from.type->text() + " ne va pas dans un " + t.text();
            return false;
        }
        into.value.assignFrom(from.value);
        return true;
    }
    if (!sameType(t, *from.type)) {
        if (why) *why = "un " + from.type->text() + " ne va pas dans un " + t.text();
        return false;
    }
    switch (t.kind) {
        case TypeDesc::Kind::Array:
        case TypeDesc::Kind::Struct:
        case TypeDesc::Kind::Tuple:
            for (std::size_t i = 0; i < into.items.size() && i < from.items.size(); ++i)
                if (into.items[i] && from.items[i] && !assignObj(*into.items[i], *from.items[i], why)) return false;
            return true;
        case TypeDesc::Kind::List: {
            if (&into == &from) return true;
            std::vector<ObjRef> copy;
            copy.reserve(from.items.size());
            for (const auto& i : from.items) copy.push_back(i ? deepCopy(*i) : makeObj(t.element));
            into.items = std::move(copy);
            normalizeList(into);                 // 1.12.7 : une LIST dans un SET (rangee, sans doublon), dans un RING (les N dernieres)
            return true;
        }
        case TypeDesc::Kind::Map:
            into.textKeys.clear();
            into.intKeys.clear();
            for (const auto& [k, v] : from.textKeys) into.textKeys.emplace(k, v ? deepCopy(*v) : nullptr);
            for (const auto& [k, v] : from.intKeys) into.intKeys.emplace(k, v ? deepCopy(*v) : nullptr);
            return true;
        default:
            into.bound = from.bound;
            into.target = from.target;
            into.targetName = from.targetName;
            into.map = from.map;
            into.textKey = from.textKey;
            into.intKey = from.intKey;
            into.atEnd = from.atEnd;
            into.callee = from.callee;
            into.lambda = from.lambda;
            into.lambdaWhere = from.lambdaWhere;
            return true;
    }
}

bool deepEquals(const Obj& a, const Obj& b) {
    if (!a.type || !b.type) return false;
    if (a.type->kind == TypeDesc::Kind::Scalar && b.type->kind == TypeDesc::Kind::Scalar) return a.value.compare(b.value) == 0;
    if (!sameType(*a.type, *b.type)) return false;
    switch (a.type->kind) {
        case TypeDesc::Kind::Array:
        case TypeDesc::Kind::Struct:
        case TypeDesc::Kind::Tuple:
        case TypeDesc::Kind::List:
            if (a.items.size() != b.items.size()) return false;
            for (std::size_t i = 0; i < a.items.size(); ++i)
                if (!a.items[i] || !b.items[i] || !deepEquals(*a.items[i], *b.items[i])) return false;
            return true;
        case TypeDesc::Kind::Map: {
            if (a.textKeys.size() != b.textKeys.size() || a.intKeys.size() != b.intKeys.size()) return false;
            for (auto i = a.textKeys.begin(), j = b.textKeys.begin(); i != a.textKeys.end(); ++i, ++j)
                if (i->first != j->first || !i->second || !j->second || !deepEquals(*i->second, *j->second)) return false;
            for (auto i = a.intKeys.begin(), j = b.intKeys.begin(); i != a.intKeys.end(); ++i, ++j)
                if (i->first != j->first || !i->second || !j->second || !deepEquals(*i->second, *j->second)) return false;
            return true;
        }
        case TypeDesc::Kind::Ref:
        case TypeDesc::Kind::Pointer:
            if (a.bound != b.bound) return false;
            if (!a.bound) return true;
            if (!a.targetName.empty() || !b.targetName.empty()) return upperOf(a.targetName) == upperOf(b.targetName);
            return !a.target.owner_before(b.target) && !b.target.owner_before(a.target) && a.target.lock() == b.target.lock();
        case TypeDesc::Kind::Callback:                       // 1.12.19 : la meme fonction (une LAMBDA : la meme)
            if (a.bound != b.bound) return false;
            if (!a.bound) return true;
            return a.lambda ? a.lambda == b.lambda : !b.lambda && upperOf(a.callee) == upperOf(b.callee);
        default: return false;
    }
}

std::string display(const Obj& o) {
    if (!o.type) return "?";
    switch (o.type->kind) {
        case TypeDesc::Kind::Scalar: return o.value.display();
        case TypeDesc::Kind::Array: {
            std::string s = "[";
            for (std::size_t i = 0; i < o.items.size(); ++i) {
                if (i == 16) { s += ", ..."; break; }
                s += (i ? ", " : "") + (o.items[i] ? display(*o.items[i]) : std::string("?"));
            }
            return s + "]";
        }
        case TypeDesc::Kind::Struct: {
            std::string s = "{";
            for (std::size_t i = 0; i < o.items.size() && i < o.type->members.size(); ++i)
                s += (i ? ", " : "") + o.type->members[i].first + ": " + (o.items[i] ? display(*o.items[i]) : std::string("?"));
            return s + "}";
        }
        case TypeDesc::Kind::Map: {
            std::string s = "{";
            bool first = true;
            for (const auto& [k, v] : o.textKeys) {
                s += (first ? "'" : ", '") + k + "': " + (v ? display(*v) : std::string("?"));
                first = false;
            }
            for (const auto& [k, v] : o.intKeys) {
                s += (first ? "" : ", ") + std::to_string(k) + ": " + (v ? display(*v) : std::string("?"));
                first = false;
            }
            return s + "}";
        }
        case TypeDesc::Kind::Ref:
        case TypeDesc::Kind::Pointer:
            if (!o.bound) return "NULL";
            return o.targetName.empty() ? (o.target.expired() ? std::string("(disparue)") : std::string("(locale)")) : "-> " + o.targetName;
        case TypeDesc::Kind::Iterator:
            return o.atEnd ? std::string("(fin)") : std::string("(cle ") + (o.textKey.empty() ? std::to_string(o.intKey) : o.textKey) + ")";
        case TypeDesc::Kind::List: {
            std::string s = "[";
            for (std::size_t i = 0; i < o.items.size(); ++i) {
                if (i == 16) { s += ", ... (" + std::to_string(o.items.size()) + ")"; break; }
                s += (i ? ", " : "") + (o.items[i] ? display(*o.items[i]) : std::string("?"));
            }
            return s + "]";
        }
        case TypeDesc::Kind::Tuple: {
            std::string s = "(";
            for (std::size_t i = 0; i < o.items.size(); ++i) s += (i ? ", " : "") + (o.items[i] ? display(*o.items[i]) : std::string("?"));
            return s + ")";
        }
        case TypeDesc::Kind::Callback:                       // 1.12.19 : la fonction visee, ou la LAMBDA et son lieu
            if (!o.bound) return "NULL";
            if (o.lambda) return "\xCE\xBB " + (o.lambdaWhere.empty() ? std::string("LAMBDA") : o.lambdaWhere);
            return "\xE2\x86\x92 " + o.callee;
    }
    return "?";
}

namespace {
std::string scalarLiteral(const Value& v) {
    switch (v.type()) {
        case Type::String: {
            std::string s = "'";
            for (const char c : v.asString()) {
                if (c == '\'') s += "$'";
                else if (c == '$') s += "$$";
                else if (c == '\n') s += "$N";
                else if (c == '\t') s += "$T";
                else s += c;
            }
            return s + "'";
        }
        case Type::Real: {
            const double r = v.asReal();
            if (!std::isfinite(r)) return "0.0";
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.15g", r);
            std::string s = buf;
            if (s.find_first_of(".eE") == std::string::npos) s += ".0";
            return s;
        }
        default: return v.display();
    }
}
} // namespace

bool literalText(const Obj& o, std::string& out) {
    if (!o.type) return false;
    switch (o.type->kind) {
        case TypeDesc::Kind::Scalar: out = scalarLiteral(o.value); return true;
        case TypeDesc::Kind::Callback:                       // 1.12.19 : un rappel s'ecrit par le nom de sa fonction
            if (!o.bound) { out = "NULL"; return true; }
            if (o.lambda || o.callee.empty()) return false;   // une LAMBDA ne se garde pas (la maquette, decision 9)
            out = o.callee;
            return true;
        case TypeDesc::Kind::Array:
        case TypeDesc::Kind::List:
        case TypeDesc::Kind::Tuple: {
            const bool tuple = o.type->kind == TypeDesc::Kind::Tuple;
            std::string s = tuple ? "(" : "[";
            for (std::size_t i = 0; i < o.items.size(); ++i) {
                std::string item;
                if (!o.items[i] || !literalText(*o.items[i], item)) return false;
                s += (i ? ", " : "") + item;
            }
            out = s + (tuple ? ")" : "]");
            return true;
        }
        case TypeDesc::Kind::Map: {
            std::string s = "[";
            bool first = true;
            for (const auto& [k, v] : o.textKeys) {
                std::string item;
                if (!v || !literalText(*v, item)) return false;
                s += (first ? "" : ", ") + scalarLiteral(Value::text(k)) + " := " + item;
                first = false;
            }
            for (const auto& [k, v] : o.intKeys) {
                std::string item;
                if (!v || !literalText(*v, item)) return false;
                s += (first ? "" : ", ") + std::to_string(k) + " := " + item;
                first = false;
            }
            out = s + "]";
            return true;
        }
        case TypeDesc::Kind::Struct: {           // 1.12.14 : (Nom := 'A', Niveau := 2.5)
            std::string s = "(";
            for (std::size_t i = 0; i < o.items.size() && i < o.type->members.size(); ++i) {
                std::string item;
                if (!o.items[i] || !literalText(*o.items[i], item)) return false;
                s += (i ? ", " : "") + o.type->members[i].first + " := " + item;
            }
            out = s + ")";
            return true;
        }
        default: return false;
    }
}

bool flatIndex(const TypeDesc& t, const std::vector<std::int64_t>& indices, std::size_t& out, std::string* why) {
    if (t.kind != TypeDesc::Kind::Array) {
        if (why) *why = "pas un tableau";
        return false;
    }
    if (indices.size() != t.bounds.size()) {
        if (why) *why = std::to_string(t.bounds.size()) + " indice(s) attendu(s), " + std::to_string(indices.size()) + " donn\xC3\xA9(s)";
        return false;
    }
    std::size_t flat = 0;
    for (std::size_t d = 0; d < indices.size(); ++d) {
        const auto [lo, hi] = t.bounds[d];
        if (indices[d] < lo || indices[d] > hi) {
            if (why)
                *why = "indice " + std::to_string(indices[d]) + " hors des bornes " + std::to_string(lo) + ".." + std::to_string(hi)
                     + (indices.size() > 1 ? " (dimension " + std::to_string(d + 1) + ")" : std::string{});
            return false;
        }
        flat = flat * static_cast<std::size_t>(hi - lo + 1) + static_cast<std::size_t>(indices[d] - lo);
    }
    out = flat;
    return true;
}

void Locals::set(std::string_view name, ObjRef obj) { vars_[upperOf(name)] = std::move(obj); }

ObjRef Locals::find(std::string_view name) const {
    const auto it = vars_.find(upperOf(name));
    return it == vars_.end() ? nullptr : it->second;
}

void Locals::erase(std::string_view name) { vars_.erase(upperOf(name)); }

} // namespace sim
