// =============================================================================
//  sim/RichPacked.cpp - 1.50.1 : LES GRANDS TABLEAUX RANGES SERRES
// -----------------------------------------------------------------------------
//  Un tableau de valeurs simples (ou de tableaux de valeurs simples) d'au moins
//  packFrom() cases en tout garde ses valeurs dans un seul bloc (PackedCells,
//  voir Rich.hpp) : un BOOL = 1 bit. Avant 1.50.1, chaque case etait un objet
//  (Obj, environ 400 octets) : 1 000 000 de cases au plus, et deja 400 Mo.
//
//  POURQUOI UN BLOC ET DES CASES SORTIES A LA DEMANDE (et pas un tableau creux) :
//  un programme d'automate parcourt ses tableaux en entier (une remise a zero,
//  une copie, un decalage) - un tableau creux finirait plein, case par case. Le
//  bloc a la taille juste des valeurs, des le depart ; seules les cases qu'on
//  TIENT (une ecriture en cours, une reference, la variable d'un FOR EACH) sont
//  des objets, le temps qu'on les tient. Le reste du moteur (les affectations,
//  les references, les fonctions) voit des cases ordinaires.
// =============================================================================
#include "Rich.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>

namespace sim {

namespace {

std::int64_t g_packFrom = 65536;      // les essais le baissent (setPackFrom) : tout range serre

constexpr std::int64_t kSaturate = std::int64_t{1} << 50;

bool packableScalar(Type t) noexcept { return t != Type::Unknown; }

// Les octets d'un entier, d'une duree, d'une date (0 : un BOOL, un REAL, un STRING).
int widthOf(Type t) noexcept {
    switch (t) {
        case Type::Bool: case Type::Real: case Type::String: case Type::Unknown: return 0;
        case Type::Byte: return 1;
        case Type::Word: case Type::Int: case Type::UInt: return 2;
        case Type::DWord: case Type::DInt: case Type::UDInt: case Type::Time: return 4;
        default: return 8;                          // LINT, ULINT, LWORD, DATE, TOD, DT
    }
}

// Les cases de tous les niveaux d'un tableau (un tableau de tableaux : leur produit) ; une valeur simple : 1.
std::int64_t leafCells(const TypeDesc& t) noexcept {
    std::int64_t n = 1;
    for (const TypeDesc* x = &t; x && x->kind == TypeDesc::Kind::Array; x = x->element.get()) {
        const std::int64_t c = x->count();
        n = c > 0 && n > kSaturate / c ? kSaturate : n * c;
    }
    return n;
}

// La valeur simple au fond d'un tableau (de tableaux) ; nul : autre chose au fond.
TypeRef leafOf(const TypeRef& t) {
    TypeRef x = t;
    while (x && x->kind == TypeDesc::Kind::Array) x = x->element;
    return x && x->kind == TypeDesc::Kind::Scalar ? x : nullptr;
}

// Les cases du bloc que couvre une case de ce niveau (1 pour une valeur simple, le sous-tableau sinon).
std::size_t spanOfElement(const TypeDesc& array) noexcept {
    return array.element && array.element->kind == TypeDesc::Kind::Array ? static_cast<std::size_t>(leafCells(*array.element)) : 1;
}

void settle(PackedCells& st) {
    for (auto it = st.live.begin(); it != st.live.end();) {
        if (it->second && it->second.use_count() == 1 && !it->second->pinned) {
            st.put(it->first, it->second->value);
            it = st.live.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = st.views.begin(); it != st.views.end();) {
        if (it->second && it->second.use_count() == 1 && !it->second->pinned) it = st.views.erase(it);
        else ++it;
    }
    st.settleAt = std::max<std::size_t>(256, (st.live.size() + st.views.size()) * 2);
}

ObjRef viewOf(const std::shared_ptr<PackedCells>& st, std::size_t first, std::size_t span, const TypeRef& type) {
    const auto key = std::make_tuple(first, span, type.get());
    if (const auto it = st->views.find(key); it != st->views.end() && it->second) return it->second;
    if (st->live.size() + st->views.size() >= st->settleAt) settle(*st);
    auto v = std::make_shared<Obj>();
    v->type = type;
    v->packed = st;
    v->packedFrom = first;
    st->views[key] = v;
    return v;
}

std::string grouped(std::int64_t n) {
    const std::string digits = std::to_string(n < 0 ? -n : n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += ' ';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}

} // namespace

// ---- le bloc ----------------------------------------------------------------------------------------------------
Value PackedCells::raw(std::size_t k) const {
    switch (scalar) {
        case Type::Bool: return Value::boolean(((bits[k >> 6] >> (k & 63)) & 1u) != 0);
        case Type::Real: return Value::real(reals[k]);
        case Type::String: return Value::text(texts[k]);
        default: {
            std::uint64_t u = 0;
            const unsigned char* at = bytes.data() + k * static_cast<std::size_t>(width);
            for (int b = width - 1; b >= 0; --b) u = (u << 8) | at[b];
            return Value::integer(scalar, static_cast<std::int64_t>(u));
        }
    }
}

void PackedCells::put(std::size_t k, const Value& given) {
    // La valeur rangee dans le type de la case (un objet sorti a pu recevoir une valeur d'un autre type).
    Value v = Value::defaultOf(scalar);
    v.assignFrom(given);
    switch (scalar) {
        case Type::Bool:
            if (v.isTruthy()) bits[k >> 6] |= std::uint64_t{1} << (k & 63);
            else bits[k >> 6] &= ~(std::uint64_t{1} << (k & 63));
            return;
        case Type::Real: reals[k] = v.asReal(); return;
        case Type::String: texts[k] = v.asString(); return;
        default: {
            auto u = static_cast<std::uint64_t>(v.asInteger());
            unsigned char* at = bytes.data() + k * static_cast<std::size_t>(width);
            for (int b = 0; b < width; ++b) {
                at[b] = static_cast<unsigned char>(u & 0xFFu);
                u >>= 8;
            }
            return;
        }
    }
}

std::size_t PackedCells::memory() const noexcept {
    std::size_t n = sizeof(PackedCells) + bits.capacity() * sizeof(std::uint64_t) + bytes.capacity() + reals.capacity() * sizeof(double)
                    + texts.capacity() * sizeof(std::string);
    for (const auto& t : texts)
        if (t.capacity() > 15) n += t.capacity() + 1;
    n += (live.size() + views.size()) * (sizeof(Obj) + 64);
    return n;
}

std::int64_t packFrom() noexcept { return g_packFrom; }
void setPackFrom(std::int64_t cells) noexcept { g_packFrom = std::max<std::int64_t>(1, cells); }

bool packable(const TypeDesc& array) noexcept {
    if (array.kind != TypeDesc::Kind::Array) return false;
    const TypeDesc* x = &array;
    while (x && x->kind == TypeDesc::Kind::Array) x = x->element.get();
    return x && x->kind == TypeDesc::Kind::Scalar && packableScalar(x->scalar);
}

// ---- la memoire -------------------------------------------------------------------------------------------------
double cellBytes(Type t) noexcept {
    switch (t) {
        case Type::Bool: return 0.125;
        case Type::Real: return 8.0;
        case Type::String: return static_cast<double>(sizeof(std::string));
        case Type::Unknown: return static_cast<double>(objectBytes());
        default: return static_cast<double>(widthOf(t));
    }
}

std::int64_t objectBytes() noexcept { return static_cast<std::int64_t>(sizeof(Obj)) + 48; }

std::int64_t memoryEstimate(const TypeDesc& t) {
    const auto add = [](std::int64_t a, std::int64_t b) { return a > kSaturate - b ? kSaturate : a + b; };
    const auto mul = [](std::int64_t a, std::int64_t b) { return a > 0 && b > kSaturate / a ? kSaturate : a * b; };
    switch (t.kind) {
        case TypeDesc::Kind::Array: {
            const std::int64_t cells = leafCells(t);
            if (packable(t) && cells >= g_packFrom) {
                const TypeDesc* x = &t;
                while (x->kind == TypeDesc::Kind::Array) x = x->element.get();
                const double bytes = cellBytes(x->scalar) * static_cast<double>(cells);
                return add(objectBytes(), bytes > static_cast<double>(kSaturate) ? kSaturate : static_cast<std::int64_t>(bytes) + 8);
            }
            const std::int64_t each = t.element ? memoryEstimate(*t.element) : objectBytes();
            return add(objectBytes(), mul(std::max<std::int64_t>(0, t.count()), add(each, static_cast<std::int64_t>(sizeof(ObjRef)))));
        }
        case TypeDesc::Kind::Struct:
        case TypeDesc::Kind::Tuple: {
            std::int64_t n = objectBytes();
            for (const auto& m : t.members)
                n = add(n, add(m.second ? memoryEstimate(*m.second) : objectBytes(), static_cast<std::int64_t>(sizeof(ObjRef))));
            return n;
        }
        default: return objectBytes();
    }
}

std::string memoryText(std::int64_t bytes) {
    char buf[64];
    const double b = static_cast<double>(bytes);
    const auto one = [&](double v, const char* unit) {
        if (v < 10.0) std::snprintf(buf, sizeof buf, "%.1f %s", v, unit);
        else std::snprintf(buf, sizeof buf, "%.0f %s", v, unit);
        std::string s = buf;
        if (const auto dot = s.find('.'); dot != std::string::npos) s[dot] = ',';
        return s;
    };
    if (bytes < 1024) return std::to_string(bytes) + " octet" + (bytes > 1 ? "s" : "");
    if (b < 1024.0 * 1024.0) return one(b / 1024.0, "ko");
    if (b < 1024.0 * 1024.0 * 1024.0) return one(b / (1024.0 * 1024.0), "Mo");
    return one(b / (1024.0 * 1024.0 * 1024.0), "Go");
}

std::string tooLargeText(const TypeDesc& t, std::int64_t bytes) {
    const std::int64_t cells = leafCells(t);
    const TypeDesc* x = &t;
    while (x && x->kind == TypeDesc::Kind::Array && x->element) x = x->element.get();
    const std::string leaf = x ? x->text() : std::string("?");
    return "tableau trop grand : " + grouped(cells) + " case" + (cells > 1 ? "s" : "") + " " + leaf + ", environ " + memoryText(bytes)
           + " en m\xC3\xA9moire (" + memoryText(kMaxValueBytes) + " au plus par valeur)";
}

// ---- un tableau neuf, range serre ---------------------------------------------------------------------------------
// (appele par makeObj : le type est un tableau rangeable et assez grand)
ObjRef makePackedArray(const TypeRef& type) {
    auto st = std::make_shared<PackedCells>();
    st->cell = leafOf(type);
    st->scalar = st->cell ? st->cell->scalar : Type::Unknown;
    st->count = static_cast<std::size_t>(std::max<std::int64_t>(0, leafCells(*type)));
    st->width = widthOf(st->scalar);
    switch (st->scalar) {
        case Type::Bool: st->bits.assign((st->count + 63) / 64, 0); break;
        case Type::Real: st->reals.assign(st->count, 0.0); break;
        case Type::String: st->texts.assign(st->count, std::string{}); break;
        default: st->bytes.assign(st->count * static_cast<std::size_t>(st->width), 0); break;
    }
    auto o = std::make_shared<Obj>();
    o->type = type;
    o->packed = std::move(st);
    o->packedFrom = 0;
    return o;
}

// La copie d'un tableau serre (ou d'une vue) : un bloc neuf, ses seules cases.
ObjRef copyPacked(const Obj& from) {
    auto o = makePackedArray(from.type);
    PackedCells& dst = *o->packed;
    const PackedCells& src = *from.packed;
    const std::size_t n = dst.count, at = from.packedFrom;
    if (src.scalar == dst.scalar) {
        switch (src.scalar) {
            case Type::Bool:
                if (at % 64 == 0) {
                    std::copy(src.bits.begin() + static_cast<std::ptrdiff_t>(at / 64),
                              src.bits.begin() + static_cast<std::ptrdiff_t>(at / 64 + dst.bits.size()), dst.bits.begin());
                    if (n % 64) dst.bits.back() &= (std::uint64_t{1} << (n % 64)) - 1;
                } else {
                    for (std::size_t k = 0; k < n; ++k)
                        if ((src.bits[(at + k) >> 6] >> ((at + k) & 63)) & 1u) dst.bits[k >> 6] |= std::uint64_t{1} << (k & 63);
                }
                break;
            case Type::Real: std::copy(src.reals.begin() + static_cast<std::ptrdiff_t>(at), src.reals.begin() + static_cast<std::ptrdiff_t>(at + n), dst.reals.begin()); break;
            case Type::String: std::copy(src.texts.begin() + static_cast<std::ptrdiff_t>(at), src.texts.begin() + static_cast<std::ptrdiff_t>(at + n), dst.texts.begin()); break;
            default:
                if (n) std::memcpy(dst.bytes.data(), src.bytes.data() + at * static_cast<std::size_t>(src.width), n * static_cast<std::size_t>(src.width));
                break;
        }
    } else {
        for (std::size_t k = 0; k < n; ++k) dst.put(k, src.raw(at + k));
    }
    // Les cases sorties font foi.
    for (const auto& [k, c] : src.live)
        if (c && k >= at && k < at + n) dst.put(k - at, c->value);
    return o;
}

// Deux tableaux serres de memes cases, rien de sorti : le bloc compare (ou copie) d'un coup. Faux : pas ce cas, case par
// case (cellValue). Les bits d'un BOOL qui ne commencent pas sur un mot : un a un.
bool packedFast(const Obj& a, const Obj& b) {
    // Les cases sorties que plus personne ne tient rentrent d'abord dans leur bloc.
    if (a.packed && !a.packed->live.empty()) settle(*a.packed);
    if (b.packed && b.packed != a.packed && !b.packed->live.empty()) settle(*b.packed);
    return a.packed && b.packed && a.items.empty() && b.items.empty() && a.packed->scalar == b.packed->scalar && a.packed->live.empty()
           && b.packed->live.empty() && a.type && b.type && leafCells(*a.type) == leafCells(*b.type);
}

bool packedEquals(const Obj& a, const Obj& b) {
    const PackedCells& x = *a.packed;
    const PackedCells& y = *b.packed;
    const std::size_t n = static_cast<std::size_t>(leafCells(*a.type)), i = a.packedFrom, j = b.packedFrom;
    switch (x.scalar) {
        case Type::Bool:
            if (i % 64 == 0 && j % 64 == 0) {
                for (std::size_t w = 0; w < n / 64; ++w)
                    if (x.bits[i / 64 + w] != y.bits[j / 64 + w]) return false;
                for (std::size_t k = n - n % 64; k < n; ++k)
                    if (((x.bits[(i + k) >> 6] >> ((i + k) & 63)) & 1u) != ((y.bits[(j + k) >> 6] >> ((j + k) & 63)) & 1u)) return false;
                return true;
            }
            for (std::size_t k = 0; k < n; ++k)
                if (((x.bits[(i + k) >> 6] >> ((i + k) & 63)) & 1u) != ((y.bits[(j + k) >> 6] >> ((j + k) & 63)) & 1u)) return false;
            return true;
        case Type::Real:
            for (std::size_t k = 0; k < n; ++k)
                if (!(x.reals[i + k] == y.reals[j + k])) return false;
            return true;
        case Type::String: return std::equal(x.texts.begin() + static_cast<std::ptrdiff_t>(i), x.texts.begin() + static_cast<std::ptrdiff_t>(i + n),
                                             y.texts.begin() + static_cast<std::ptrdiff_t>(j));
        default:
            return n == 0 || std::memcmp(x.bytes.data() + i * static_cast<std::size_t>(x.width), y.bytes.data() + j * static_cast<std::size_t>(y.width),
                                         n * static_cast<std::size_t>(x.width)) == 0;
    }
}

void packedAssign(const Obj& into, const Obj& from) {
    PackedCells& x = *into.packed;
    const PackedCells& y = *from.packed;
    const std::size_t n = static_cast<std::size_t>(leafCells(*into.type)), i = into.packedFrom, j = from.packedFrom;
    if (&x == &y && i == j) return;
    switch (x.scalar) {
        case Type::Bool:
            if (i % 64 == 0 && j % 64 == 0 && &x != &y) {
                std::copy(y.bits.begin() + static_cast<std::ptrdiff_t>(j / 64), y.bits.begin() + static_cast<std::ptrdiff_t>(j / 64 + n / 64),
                          x.bits.begin() + static_cast<std::ptrdiff_t>(i / 64));
                for (std::size_t k = n - n % 64; k < n; ++k) x.put(i + k, y.raw(j + k));
                return;
            }
            for (std::size_t k = 0; k < n; ++k) x.put(i + k, y.raw(j + k));
            return;
        case Type::Real:
            std::copy(y.reals.begin() + static_cast<std::ptrdiff_t>(j), y.reals.begin() + static_cast<std::ptrdiff_t>(j + n),
                      x.reals.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        case Type::String:
            for (std::size_t k = 0; k < n; ++k) x.texts[i + k] = y.texts[j + k];
            return;
        default:
            if (n) std::memmove(x.bytes.data() + i * static_cast<std::size_t>(x.width), y.bytes.data() + j * static_cast<std::size_t>(y.width),
                                n * static_cast<std::size_t>(x.width));
            return;
    }
}

// ---- les cases ----------------------------------------------------------------------------------------------------
std::size_t cellCount(const Obj& array) noexcept {
    if (array.packed && array.items.empty() && array.type) return static_cast<std::size_t>(std::max<std::int64_t>(0, array.type->count()));
    return array.items.size();
}

ObjRef cellAt(const Obj& array, std::size_t k) {
    if (!array.packed || !array.items.empty()) return k < array.items.size() ? array.items[k] : nullptr;
    if (!array.type || k >= cellCount(array)) return nullptr;
    const auto& st = array.packed;
    const TypeRef& el = array.type->element;
    if (el && el->kind == TypeDesc::Kind::Array) {
        const std::size_t span = static_cast<std::size_t>(leafCells(*el));
        return viewOf(st, array.packedFrom + k * span, span, el);
    }
    const std::size_t at = array.packedFrom + k;
    if (const auto it = st->live.find(at); it != st->live.end()) return it->second;
    if (st->live.size() + st->views.size() >= st->settleAt) settle(*st);
    auto c = std::make_shared<Obj>();
    c->type = el ? el : st->cell;
    c->value = st->raw(at);
    st->live.emplace(at, c);
    return c;
}

Value cellValue(const Obj& array, std::size_t k) {
    if (!array.packed || !array.items.empty()) return k < array.items.size() && array.items[k] ? array.items[k]->value : Value{};
    const std::size_t at = array.packedFrom + k;
    const auto& st = *array.packed;
    if (!st.live.empty())
        if (const auto it = st.live.find(at); it != st.live.end() && it->second) return it->second->value;
    return at < st.count ? st.raw(at) : Value{};
}

void setCellValue(const Obj& array, std::size_t k, const Value& v) {
    if (!array.packed || !array.items.empty()) {
        if (k < array.items.size() && array.items[k]) array.items[k]->value.assignFrom(v);
        return;
    }
    const std::size_t at = array.packedFrom + k;
    auto& st = *array.packed;
    if (!st.live.empty())
        if (const auto it = st.live.find(at); it != st.live.end() && it->second) {
            it->second->value.assignFrom(v);
            return;
        }
    if (at < st.count) st.put(at, v);
}

ObjRef subView(const Obj& array, std::size_t first, std::size_t span, TypeRef type) {
    if (array.packed && array.items.empty() && array.type) {
        const std::size_t each = spanOfElement(*array.type);
        return viewOf(array.packed, array.packedFrom + first * each, span * each, type);
    }
    auto row = std::make_shared<Obj>();
    row->type = std::move(type);
    if (first <= array.items.size())
        row->items.assign(array.items.begin() + static_cast<std::ptrdiff_t>(first),
                          array.items.begin() + static_cast<std::ptrdiff_t>(std::min(array.items.size(), first + span)));
    return row;
}

bool unpack(Obj& array, std::int64_t maxCells, std::string* why) {
    if (!array.packed || !array.items.empty()) return true;
    const std::size_t n = cellCount(array);
    if (static_cast<std::int64_t>(n) > maxCells) {
        if (why)
            *why = "ce tableau de " + grouped(static_cast<std::int64_t>(n)) + " cases est rang\xC3\xA9 serr\xC3\xA9 : cette fonction en prend "
                   + grouped(maxCells) + " au plus (parcours-le par ses indices, FOR i := ...)";
        return false;
    }
    std::vector<ObjRef> cells;
    cells.reserve(n);
    for (std::size_t k = 0; k < n; ++k) cells.push_back(cellAt(array, k));
    array.items = std::move(cells);
    return true;
}

bool fillCells(const Obj& array, const Value& v) {
    if (!array.type || array.type->kind != TypeDesc::Kind::Array) return false;
    const TypeRef& el = array.type->element;
    if (!el) return false;
    if (el->kind == TypeDesc::Kind::Array) {
        for (std::size_t k = 0, n = cellCount(array); k < n; ++k) {
            const ObjRef row = cellAt(array, k);
            if (!row || !fillCells(*row, v)) return false;
        }
        return true;
    }
    if (el->kind != TypeDesc::Kind::Scalar) return false;
    if (array.packed && array.items.empty() && array.packed->live.empty()) {
        // Le bloc directement : la valeur convertie une fois, rangee partout.
        PackedCells& st = *array.packed;
        const std::size_t n = cellCount(array);
        for (std::size_t k = 0; k < n; ++k) st.put(array.packedFrom + k, v);
        return true;
    }
    for (std::size_t k = 0, n = cellCount(array); k < n; ++k) setCellValue(array, k, v);
    return true;
}

void pin(const ObjRef& o) noexcept {
    if (o) o->pinned = true;
}

std::size_t heldMemory(const Obj& o) {
    std::size_t n = sizeof(Obj);
    if (o.packed) n += o.packed->memory();
    for (const auto& i : o.items)
        if (i && !(o.packed && i->packed == o.packed)) n += heldMemory(*i) + sizeof(ObjRef) + 32;
    for (const auto& [k, v] : o.textKeys) n += k.size() + (v ? heldMemory(*v) : 0) + 64;
    for (const auto& [k, v] : o.intKeys) n += (v ? heldMemory(*v) : 0) + 64;
    return n;
}

} // namespace sim
