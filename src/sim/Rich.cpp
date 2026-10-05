#include "Rich.hpp"

#include <algorithm>
#include <cctype>

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
    }
    return "?";
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
            o->items.reserve(type->members.size());
            for (const auto& m : type->members) o->items.push_back(makeObj(m.second));
            break;
        default: break;
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
            for (std::size_t i = 0; i < into.items.size() && i < from.items.size(); ++i)
                if (into.items[i] && from.items[i] && !assignObj(*into.items[i], *from.items[i], why)) return false;
            return true;
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
    }
    return "?";
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
