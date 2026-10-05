// hmi/HmiTwin.cpp - le jumeau simule d'un equipement (lot 17).
#include "HmiTwin.hpp"

#include "HmiEquipment.hpp"
#include "HmiTypes.hpp"
#include "HmiZones.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace hmi::twin {

namespace {

constexpr std::uint32_t kSpace = 65536;
constexpr double kPi = 3.14159265358979323846;

std::size_t idx(MemTable t) { return static_cast<std::size_t>(t); }

sim::Type typeOf(std::string_view name) { return equip::typeOfName(name); }

bool isBoolType(std::string_view t) {
    return t.size() == 4 && (t[0] == 'B' || t[0] == 'b') && (t[1] == 'O' || t[1] == 'o') && (t[2] == 'O' || t[2] == 'o') && (t[3] == 'L' || t[3] == 'l');
}

// Les mots d'une valeur, dans l'ordre de l'equipement.
std::vector<std::uint16_t> encode(double v, std::string_view type, bool low) {
    const std::string t = [&] {
        std::string s(type);
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    }();
    const auto two = [&](std::uint32_t bits) {
        const auto lo = static_cast<std::uint16_t>(bits & 0xFFFF), hi = static_cast<std::uint16_t>(bits >> 16);
        return low ? std::vector<std::uint16_t>{lo, hi} : std::vector<std::uint16_t>{hi, lo};
    };
    if (t == "REAL" || t == "LREAL") {
        const float f = static_cast<float>(v);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof bits);
        return two(bits);
    }
    if (t == "DINT") return two(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::clamp(std::llround(v), -2147483648LL, 2147483647LL))));
    if (t == "UDINT" || t == "DWORD" || t == "TIME") return two(static_cast<std::uint32_t>(std::clamp(std::llround(v), 0LL, 4294967295LL)));
    if (t == "UINT" || t == "WORD") return {static_cast<std::uint16_t>(std::clamp(std::llround(v), 0LL, 65535LL))};
    return {static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(std::llround(v), -32768LL, 32767LL)))};
}

double decode(const std::vector<std::uint16_t>& w, std::string_view type, bool low) {
    std::string t(type);
    for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (w.empty()) return 0;
    const auto two = [&]() -> std::uint32_t {
        if (w.size() < 2) return w[0];
        return low ? (static_cast<std::uint32_t>(w[1]) << 16) | w[0] : (static_cast<std::uint32_t>(w[0]) << 16) | w[1];
    };
    if (t == "REAL" || t == "LREAL") {
        const std::uint32_t bits = two();
        float f = 0;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }
    if (t == "DINT") return static_cast<std::int32_t>(two());
    if (t == "UDINT" || t == "DWORD" || t == "TIME") return two();
    if (t == "UINT" || t == "WORD") return w[0];
    return static_cast<std::int16_t>(w[0]);
}

struct Place {
    MemTable      table{MemTable::Holding};
    std::uint32_t offset{0};
    int           bit{-1};
    std::uint32_t words{1};
    bool          boolean{false};
};
bool placeOf(const Behavior& b, Place& out, std::string* why) {
    comm::Point pt;
    const sim::Type st = isBoolType(b.type) ? sim::Type::Bool : typeOf(b.type);
    if (!equip::placeEquipmentAddress(b.address, st, pt, why)) return false;
    out.table = zones::tableOf(pt.area);
    out.offset = pt.offset;
    out.bit = pt.encoding == comm::Encoding::BitOfWord ? pt.bit : -1;
    out.boolean = pt.bits() || out.bit >= 0 || st == sim::Type::Bool;
    out.words = out.boolean ? 1 : std::max<std::uint32_t>(1, pt.size);
    return true;
}

double readPlace(const TwinBank& bank, const Place& p, std::string_view type, bool low) {
    if (zones::isBits(p.table)) return bank.bit(p.table, p.offset) ? 1 : 0;
    if (p.bit >= 0) return (bank.word(p.table, p.offset) >> p.bit) & 1 ? 1 : 0;
    return decode(bank.words(p.table, p.offset, p.words), type, low);
}

void writePlace(TwinBank& bank, const Place& p, double v, std::string_view type, bool low) {
    if (zones::isBits(p.table)) {
        bank.setBit(p.table, p.offset, v != 0);
        return;
    }
    if (p.bit >= 0) {
        std::uint16_t w = bank.word(p.table, p.offset);
        if (v != 0) w = static_cast<std::uint16_t>(w | (1u << p.bit));
        else w = static_cast<std::uint16_t>(w & ~(1u << p.bit));
        bank.setWord(p.table, p.offset, w);
        return;
    }
    const auto w = encode(v, type, low);
    for (std::size_t i = 0; i < w.size(); ++i) bank.setWord(p.table, p.offset + static_cast<std::uint32_t>(i), w[i]);
}

std::vector<double> stepsOf(const std::string& s) {
    std::vector<double> out;
    std::string cur;
    const auto flush = [&] {
        if (cur.empty()) return;
        try {
            std::string c = cur;
            std::replace(c.begin(), c.end(), ',', '.');
            out.push_back(std::stod(c));
        } catch (...) {
        }
        cur.clear();
    };
    for (char c : s) {
        if (c == ';' || c == ' ' || c == '|') flush();
        else cur.push_back(c);
    }
    flush();
    return out;
}

std::string num(double v) {
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e12) std::snprintf(b, sizeof b, "%.0f", v);
    else std::snprintf(b, sizeof b, "%.6g", v);
    std::string s = b;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

// La valeur initiale d'une variable (comme l'equipement simule du lot 15).
sim::Value initialOf(const Variable& v) {
    const sim::Type t = equip::typeOfName(v.type);
    std::string s;
    {
        std::size_t a = 0, b = v.initial.size();
        while (a < b && std::isspace(static_cast<unsigned char>(v.initial[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(v.initial[b - 1]))) --b;
        s = v.initial.substr(a, b - a);
    }
    std::string up = s;
    for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (t == sim::Type::Bool) return sim::Value::boolean(up == "TRUE" || up == "1");
    if (t == sim::Type::String) {
        std::string text = s;
        if (text.size() >= 2 && (text.front() == '\'' || text.front() == '"') && text.back() == text.front()) text = text.substr(1, text.size() - 2);
        return sim::Value::text(text);
    }
    char* end = nullptr;
    const double d = up.empty() ? 0.0 : std::strtod(up.c_str(), &end);
    if (t == sim::Type::Real) return sim::Value::real(d);
    if (t == sim::Type::Time) return sim::Value::time(static_cast<std::int64_t>(std::llround(d)));
    return sim::Value::integer(t, static_cast<std::int64_t>(std::llround(d)));
}

// Le client en cours de service (le serveur le pose), ou l'application.
std::string whoNow() {
    std::string c = modbus::servingClient();
    return c.empty() ? std::string("l'application") : c;
}

} // namespace

double now() noexcept {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

TwinBank::TwinBank() {
    for (std::size_t t = 0; t < 4; ++t) {
        mem_[t].assign(kSpace, 0);
        cells_[t].assign(kSpace, Cell{});
    }
}

void TwinBank::setZones(const MemZones& z) {
    std::lock_guard<std::mutex> lock(mutex_);
    zones_ = z;
}
MemZones TwinBank::zones() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_;
}
void TwinBank::setForcedException(int code) {
    std::lock_guard<std::mutex> lock(mutex_);
    forced_ = code;
}
int TwinBank::forcedException() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return forced_;
}
void TwinBank::setIdentification(modbus::Identification id) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_ = std::move(id);
}
modbus::Identification TwinBank::identification() {
    std::lock_guard<std::mutex> lock(mutex_);
    return id_;
}

int TwinBank::check(MemTable t, std::uint32_t first, std::uint32_t count) const {
    if (forced_) return forced_;
    if (static_cast<std::uint64_t>(first) + count > kSpace) return 2;
    if (!zones_.declared) return 0;
    const auto& r = zones_.of(t);
    if (r.empty()) return 1;                  // la table n'existe pas
    return zones::contains(r, first, count) ? 0 : 2;
}

std::uint16_t TwinBank::nameIndex(const std::string& who) {
    for (std::size_t i = 1; i < names_.size(); ++i)
        if (names_[i] == who) return static_cast<std::uint16_t>(i);
    if (names_.size() >= 60000) return 0;
    names_.push_back(who);
    return static_cast<std::uint16_t>(names_.size() - 1);
}

void TwinBank::touch(MemTable t, std::uint32_t first, std::uint32_t count, bool write, const std::string& who) {
    // Les lectures du scanner de la carte et de la detection ne comptent pas :
    // elles liraient tout, tout le temps, et cacheraient qui lit vraiment.
    if (!write && (who.rfind("Scanner", 0) == 0 || who.rfind("D\xC3\xA9tection", 0) == 0)) return;
    const float at = static_cast<float>(now());
    const std::uint16_t n = nameIndex(who);
    auto& cells = cells_[idx(t)];
    for (std::uint32_t i = 0; i < count && first + i < kSpace; ++i) {
        auto& c = cells[first + i];
        if (write) {
            c.writeAt = at;
            c.writer = n;
        } else {
            c.readAt = at;
            c.reader = n;
        }
    }
}

int TwinBank::readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) {
    const MemTable t = discrete ? MemTable::DiscreteInputs : MemTable::Coils;
    const std::string who = whoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    if (const int e = check(t, address, count)) {
        ++counters_.refused;
        return e;
    }
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) out[i] = mem_[idx(t)][address + i] != 0;
    touch(t, address, count, false, who);
    ++counters_.reads;
    return 0;
}

int TwinBank::readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) {
    const MemTable t = input ? MemTable::InputRegisters : MemTable::Holding;
    const std::string who = whoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    if (const int e = check(t, address, count)) {
        ++counters_.refused;
        return e;
    }
    out.assign(mem_[idx(t)].begin() + address, mem_[idx(t)].begin() + address + count);
    touch(t, address, count, false, who);
    ++counters_.reads;
    return 0;
}

int TwinBank::writeBits(std::uint16_t address, const std::vector<bool>& values) {
    const std::string who = whoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto n = static_cast<std::uint32_t>(values.size());
    if (const int e = check(MemTable::Coils, address, n)) {
        ++counters_.refused;
        return e;
    }
    // Lot 18 : une bobine forcee ne change pas - la requete est refusee toute entiere.
    for (std::uint32_t i = 0; i < n; ++i)
        if (changesForced(MemTable::Coils, address + i, values[i] ? 1 : 0)) {
            refuseForced(MemTable::Coils, address + i, who);
            return 4;
        }
    for (std::uint32_t i = 0; i < n; ++i) mem_[idx(MemTable::Coils)][address + i] = values[i] ? 1 : 0;
    touch(MemTable::Coils, address, n, true, who);
    ++counters_.writes;
    counters_.lastWriteAt = now();
    counters_.lastWrite = zones::modicon(MemTable::Coils, address) + " = " + (values.empty() ? "?" : values[0] ? "1" : "0") + " (" + who + ")";
    return 0;
}

int TwinBank::writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) {
    const std::string who = whoNow();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto n = static_cast<std::uint32_t>(values.size());
    if (const int e = check(MemTable::Holding, address, n)) {
        ++counters_.refused;
        return e;
    }
    // Lot 18 : un registre force (ou un de ses bits) ne change pas - refusee toute entiere.
    for (std::uint32_t i = 0; i < n; ++i)
        if (changesForced(MemTable::Holding, address + i, values[i])) {
            refuseForced(MemTable::Holding, address + i, who);
            return 4;
        }
    for (std::uint32_t i = 0; i < n; ++i) mem_[idx(MemTable::Holding)][address + i] = hold(MemTable::Holding, address + i, values[i]);
    touch(MemTable::Holding, address, n, true, who);
    ++counters_.writes;
    counters_.lastWriteAt = now();
    counters_.lastWrite = zones::modicon(MemTable::Holding, address) + " = " + (values.empty() ? std::string("?") : std::to_string(values[0]))
                          + (n > 1 ? " (+" + std::to_string(n - 1) + ")" : std::string{}) + " (" + who + ")";
    return 0;
}

std::uint16_t TwinBank::word(MemTable t, std::uint32_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return offset < kSpace ? mem_[idx(t)][offset] : 0;
}
void TwinBank::setWord(MemTable t, std::uint32_t offset, std::uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (offset < kSpace) mem_[idx(t)][offset] = hold(t, offset, zones::isBits(t) ? (value ? 1 : 0) : value);
}
bool TwinBank::bit(MemTable t, std::uint32_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return offset < kSpace && mem_[idx(t)][offset] != 0;
}
void TwinBank::setBit(MemTable t, std::uint32_t offset, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (offset < kSpace) mem_[idx(t)][offset] = hold(t, offset, value ? 1 : 0);
}
std::vector<std::uint16_t> TwinBank::words(MemTable t, std::uint32_t first, std::uint32_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::uint16_t> out;
    for (std::uint32_t i = 0; i < count && first + i < kSpace; ++i) out.push_back(mem_[idx(t)][first + i]);
    return out;
}
int TwinBank::write(MemTable t, std::uint32_t offset, const std::vector<std::uint16_t>& values, const std::string& who) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto n = static_cast<std::uint32_t>(values.size());
    for (std::uint32_t i = 0; i < n && offset + i < kSpace; ++i)
        if (changesForced(t, offset + i, zones::isBits(t) ? (values[i] ? 1 : 0) : values[i])) {
            refuseForced(t, offset + i, who);
            return 4;
        }
    for (std::uint32_t i = 0; i < n && offset + i < kSpace; ++i)
        mem_[idx(t)][offset + i] = hold(t, offset + i, zones::isBits(t) ? (values[i] ? 1 : 0) : values[i]);
    touch(t, offset, n, true, who);
    ++counters_.writes;
    counters_.lastWriteAt = now();
    counters_.lastWrite = zones::modicon(t, offset) + " = " + (values.empty() ? std::string("?") : std::to_string(values[0])) + " (" + who + ")";
    return 0;
}
void TwinBank::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& m : mem_) std::fill(m.begin(), m.end(), 0);
    for (const auto t : kMemTables)
        for (const auto& [off, mv] : force_[idx(t)]) mem_[idx(t)][off] = hold(t, off, 0);
}

// ---------------------------------------------------------- lot 18 : forcer ---
std::uint16_t TwinBank::hold(MemTable t, std::uint32_t offset, std::uint16_t value) const {
    const auto& f = force_[idx(t)];
    const auto it = f.find(offset);
    if (it == f.end()) return value;
    return static_cast<std::uint16_t>((value & ~it->second.first) | (it->second.second & it->second.first));
}

bool TwinBank::changesForced(MemTable t, std::uint32_t offset, std::uint16_t value) const {
    const auto& f = force_[idx(t)];
    const auto it = f.find(offset);
    return it != f.end() && ((value ^ it->second.second) & it->second.first) != 0;
}

void TwinBank::refuseForced(MemTable t, std::uint32_t offset, const std::string& who) {
    ++counters_.refused;
    ++counters_.forcedRefused;
    counters_.lastRefusedAt = now();
    counters_.lastRefused = zones::modicon(t, offset) + " (" + who + ")";
}

void TwinBank::setForced(const std::vector<ForcedCell>& cells) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& f : force_) f.clear();
    for (const auto& c : cells) {
        if (c.offset >= kSpace) continue;
        auto& slot = force_[idx(c.table)][c.offset];
        const std::uint16_t mask = zones::isBits(c.table) ? 1 : c.mask;
        slot.second = static_cast<std::uint16_t>((slot.second & ~mask) | (c.value & mask));
        slot.first = static_cast<std::uint16_t>(slot.first | mask);
    }
    for (const auto t : kMemTables)
        for (const auto& [off, mv] : force_[idx(t)]) mem_[idx(t)][off] = hold(t, off, mem_[idx(t)][off]);
}

bool TwinBank::forced(MemTable t, std::uint32_t first, std::uint32_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& f = force_[idx(t)];
    for (std::uint32_t i = 0; i < count; ++i)
        if (f.count(first + i)) return true;
    return false;
}

std::uint16_t TwinBank::forcedMask(MemTable t, std::uint32_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& f = force_[idx(t)];
    const auto it = f.find(offset);
    return it == f.end() ? 0 : it->second.first;
}

TwinBank::Access TwinBank::access(MemTable t, std::uint32_t offset) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Access a;
    if (offset >= kSpace) return a;
    const auto& c = cells_[idx(t)][offset];
    a.readAt = c.readAt;
    a.writeAt = c.writeAt;
    if (c.reader < names_.size()) a.reader = names_[c.reader];
    if (c.writer < names_.size()) a.writer = names_[c.writer];
    return a;
}

TwinBank::Counters TwinBank::counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return counters_;
}

std::vector<SavedMemory> TwinBank::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SavedMemory> out;
    for (const auto t : kMemTables) {
        const auto& m = mem_[idx(t)];
        std::uint32_t i = 0;
        while (i < kSpace) {
            if (m[i] == 0) {
                ++i;
                continue;
            }
            SavedMemory s;
            s.table = t;
            s.first = i;
            // Une suite : jusqu'a 8 zeros de suite (au-dela, une nouvelle suite).
            std::uint32_t zeros = 0, j = i;
            while (j < kSpace && zeros <= 8) {
                s.values.push_back(m[j]);
                zeros = m[j] == 0 ? zeros + 1 : 0;
                ++j;
            }
            while (!s.values.empty() && s.values.back() == 0) s.values.pop_back();
            i = s.first + static_cast<std::uint32_t>(s.values.size());
            out.push_back(std::move(s));
        }
    }
    return out;
}

void TwinBank::load(const std::vector<SavedMemory>& saved) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : saved)
        for (std::size_t i = 0; i < s.values.size() && s.first + i < kSpace; ++i) {
            const auto off = static_cast<std::uint32_t>(s.first + i);
            mem_[idx(s.table)][off] = hold(s.table, off, zones::isBits(s.table) ? (s.values[i] ? 1 : 0) : s.values[i]);
        }
}

bool forcedCells(const Forcing& f, bool low, std::vector<TwinBank::ForcedCell>& out, std::string* why) {
    Behavior b;
    b.address = f.address;
    b.type = f.type;
    Place p;
    if (!placeOf(b, p, why)) return false;
    if (zones::isBits(p.table)) {
        out.push_back({p.table, p.offset, 1, static_cast<std::uint16_t>(f.value != 0 ? 1 : 0)});
        return true;
    }
    if (p.bit >= 0) {
        const auto mask = static_cast<std::uint16_t>(1u << p.bit);
        out.push_back({p.table, p.offset, mask, static_cast<std::uint16_t>(f.value != 0 ? mask : 0)});
        return true;
    }
    if (isBoolType(f.type)) {
        out.push_back({p.table, p.offset, 0xFFFF, static_cast<std::uint16_t>(f.value != 0 ? 1 : 0)});
        return true;
    }
    const auto w = encode(f.value, f.type, low);
    for (std::size_t i = 0; i < w.size(); ++i) out.push_back({p.table, p.offset + static_cast<std::uint32_t>(i), 0xFFFF, w[i]});
    return true;
}

std::vector<TwinBank::ForcedCell> forcedCells(const std::vector<Forcing>& list, bool low) {
    std::vector<TwinBank::ForcedCell> out;
    for (const auto& f : list) (void)forcedCells(f, low, out, nullptr);
    return out;
}

std::string forcingText(const Forcing& f) {
    if (isBoolType(f.type)) return f.value != 0 ? "1" : "0";
    return num(f.value) + " (" + f.type + ")";
}

void primeInitial(TwinBank& bank, const Project& p, const Equipment& e) {
    for (const auto* top : equip::boundVariables(p, e)) primeVariable(bank, p, e, *top);
}

void primeVariable(TwinBank& bank, const Project& p, const Equipment& e, const Variable& top) {
    const bool low = e.wordOrder != "fort";
    {
        for (const auto& v : types::leafVariables(p, top)) {
            comm::Point pt;
            if (v.address.empty() || !equip::placeEquipmentAddress(v.address, equip::registerType(v), pt)) continue;
            const sim::Value raw = equip::toRegister(v, initialOf(v));
            const MemTable t = zones::tableOf(pt.area);
            if (pt.bits()) {
                bank.setBit(t, pt.offset, raw.isTruthy());
            } else {
                const auto words = comm::encodeWords(pt, raw, low, bank.word(t, pt.offset));
                for (std::size_t i = 0; i < words.size(); ++i) bank.setWord(t, pt.offset + static_cast<std::uint32_t>(i), words[i]);
            }
        }
    }
}

// ------------------------------------------------------------ les comportements ---
void Behaviors::tick(TwinBank& bank, const std::vector<Behavior>& list, double t, bool low, const PlcReader& plc) {
    for (const auto& b : list) {
        if (!b.enabled) continue;                    // lot 18 : decoche, il ne fait rien
        Place p;
        if (!placeOf(b, p, nullptr)) continue;
        const std::string key = b.address + "|" + std::string(behaviorKindKey(b.kind)) + "|" + b.type;
        auto& st = state_[key];
        const double period = b.period > 1e-3 ? b.period : 1.0;
        double v = 0;
        bool write = true;
        switch (b.kind) {
            case BehaviorKind::Constant:
                v = b.a;
                break;
            case BehaviorKind::Sine:
                v = (b.a + b.b) / 2 + (b.b - b.a) / 2 * std::sin(2 * kPi * t / period);
                break;
            case BehaviorKind::Ramp: {
                const double f = std::fmod(t, period) / period;
                v = b.a + (b.b - b.a) * f;
                break;
            }
            case BehaviorKind::Counter:
                v = b.a + b.b * std::floor(t / period);
                break;
            case BehaviorKind::Blink:
                // Lot 18 : a 1 pendant "delay" s toutes les "period" s ; sans : une periode sur deux.
                v = b.delay > 0 ? (std::fmod(t, period) < b.delay ? 1 : 0) : static_cast<long long>(std::floor(t / period)) % 2 == 0 ? 1 : 0;
                break;
            case BehaviorKind::Random:
                if (t >= st.nextAt) {
                    st.nextAt = t + period;
                    std::uniform_real_distribution<double> d(std::min(b.a, b.b), std::max(b.a, b.b));
                    st.value = d(rng_);
                }
                v = st.value;
                break;
            case BehaviorKind::Copy: {
                Behavior src = b;
                src.address = b.source;
                Place sp;
                if (!placeOf(src, sp, nullptr)) {
                    write = false;
                    break;
                }
                const double now1 = readPlace(bank, sp, b.type, low);
                st.history.emplace_back(t, now1);
                while (st.history.size() > 2 && st.history[1].first <= t - b.delay) st.history.pop_front();
                v = b.delay <= 0 ? now1 : st.history.front().first <= t - b.delay ? st.history.front().second : readPlace(bank, p, b.type, low);
                if (st.history.size() > 20000) st.history.pop_front();
                break;
            }
            case BehaviorKind::FollowPlc: {
                const auto x = plc ? plc(b.source) : std::nullopt;
                if (!x) {
                    write = false;
                    break;
                }
                v = *x;
                break;
            }
            case BehaviorKind::Steps: {
                const auto list1 = stepsOf(b.source);
                if (list1.empty()) {
                    write = false;
                    break;
                }
                v = list1[static_cast<std::size_t>(std::floor(t / period)) % list1.size()];
                break;
            }
        }
        // Lot 18 : un bruit (+/- noise, brut) sur une valeur qui n'est pas un bit.
        if (write && b.noise > 0 && !p.boolean) {
            std::uniform_real_distribution<double> d(-b.noise, b.noise);
            v += d(rng_);
        }
        if (write) writePlace(bank, p, v, b.type, low);
    }
}

std::optional<double> valueAt(const TwinBank& bank, const Behavior& b, bool low) {
    Place p;
    if (!placeOf(b, p, nullptr)) return std::nullopt;
    return readPlace(bank, p, b.type, low);
}

bool validBehavior(const Behavior& b, std::string* why) {
    Place p;
    if (b.address.empty()) {
        if (why) *why = "sans adresse";
        return false;
    }
    if (!placeOf(b, p, why)) return false;
    if (b.kind == BehaviorKind::Copy) {
        Behavior src = b;
        src.address = b.source;
        Place sp;
        if (b.source.empty() || !placeOf(src, sp, why)) {
            if (why && b.source.empty()) *why = "recopie : l'adresse \xC3\xA0 recopier manque";
            return false;
        }
    }
    if (b.kind == BehaviorKind::FollowPlc && b.source.empty()) {
        if (why) *why = "suit l'automate : la variable manque";
        return false;
    }
    if (b.kind == BehaviorKind::Steps && stepsOf(b.source).empty()) {
        if (why) *why = "\xC3\xA9tapes : la liste des valeurs manque (1; 5; 3)";
        return false;
    }
    if ((b.kind == BehaviorKind::Sine || b.kind == BehaviorKind::Ramp || b.kind == BehaviorKind::Counter || b.kind == BehaviorKind::Blink
         || b.kind == BehaviorKind::Random || b.kind == BehaviorKind::Steps) && b.period <= 0) {
        if (why) *why = "p\xC3\xA9riode nulle";
        return false;
    }
    return true;
}

std::string behaviorText(const Behavior& b) {
    if (b.noise > 0) {
        Behavior quiet = b;
        quiet.noise = 0;
        return behaviorText(quiet) + ", bruit \xC2\xB1 " + num(b.noise);
    }
    switch (b.kind) {
        case BehaviorKind::Constant: return "vaut " + num(b.a);
        case BehaviorKind::Sine: return num(b.a) + " \xE2\x86\x92 " + num(b.b) + ", p\xC3\xA9riode " + num(b.period) + " s";
        case BehaviorKind::Ramp: return num(b.a) + " \xE2\x86\x92 " + num(b.b) + " en " + num(b.period) + " s, puis repart";
        case BehaviorKind::Counter: return "part de " + num(b.a) + ", +" + num(b.b) + " toutes les " + num(b.period) + " s";
        case BehaviorKind::Blink:
            return b.delay > 0 ? "\xC3\xA0 1 pendant " + num(b.delay) + " s toutes les " + num(b.period) + " s" : "toutes les " + num(b.period) + " s";
        case BehaviorKind::Random: return num(b.a) + " \xE2\x86\x92 " + num(b.b) + ", toutes les " + num(b.period) + " s";
        case BehaviorKind::Copy: return "de " + b.source + (b.delay > 0 ? ", apr\xC3\xA8s " + num(b.delay) + " s" : std::string{});
        case BehaviorKind::FollowPlc: return "= " + b.source + " (le simulateur)";
        case BehaviorKind::Steps: return b.source + ", une toutes les " + num(b.period) + " s";
    }
    return {};
}

// ------------------------------------------------- lot 18 : les valeurs simulees ---
double ValueRow::eng(double r) const noexcept {
    if (!scaled || rawMax == rawMin) return r;
    return engMin + (r - rawMin) * (engMax - engMin) / (rawMax - rawMin);
}
double ValueRow::raw(double e) const noexcept {
    if (!scaled || engMax == engMin) return e;
    return rawMin + (e - engMin) * (rawMax - rawMin) / (engMax - engMin);
}

namespace {

struct CellKey {
    MemTable      table{MemTable::Holding};
    std::uint32_t offset{0};
    int           bit{-1};
    bool          ok{false};
    bool operator==(const CellKey& o) const { return ok && o.ok && table == o.table && offset == o.offset && bit == o.bit; }
};
CellKey cellOf(const std::string& address, const std::string& type) {
    Behavior b;
    b.address = address;
    b.type = type.empty() ? std::string("INT") : type;
    Place p;
    if (!placeOf(b, p, nullptr)) {
        b.type = "BOOL";                          // un bit de mot (%MW10.3) ne se place qu'en BOOL
        if (!placeOf(b, p, nullptr)) return {};
    }
    return {p.table, p.offset, p.bit, true};
}

bool isRealType(std::string_view t) {
    std::string u(t);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u == "REAL" || u == "LREAL";
}

bool unsignedType(std::string_view t) {
    std::string u(t);
    for (auto& c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return u == "UINT" || u == "WORD" || u == "UDINT" || u == "DWORD" || u == "TIME";
}

// Des bornes rondes autour de [lo, hi].
std::pair<double, double> niceAround(double lo, double hi) {
    double span = hi - lo;
    if (span <= 0) span = std::max(1.0, std::fabs(hi) * 0.2);
    double a = lo - span * 0.25, c = hi + span * 0.25;
    if (lo >= 0 && a < 0) a = 0;
    const double raw = (c - a) / 5;
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
    const double step = raw / mag < 1.5 ? mag : raw / mag < 3.5 ? 2 * mag : raw / mag < 7.5 ? 5 * mag : 10 * mag;
    a = std::floor(a / step) * step;
    c = std::ceil(c / step) * step;
    if (c <= a) c = a + step;
    return {a, c};
}

} // namespace

bool sameCell(const std::string& a, const std::string& b) {
    if (a == b) return true;
    return cellOf(a, "INT") == cellOf(b, "INT");
}

std::vector<ValueRow> valueRows(const Project& p, const Equipment& e) {
    std::vector<ValueRow> out;
    std::vector<CellKey> keys;
    const auto m = zones::buildMap(p, e);
    for (const auto& mv : m.vars) {
        std::string up = mv.type;
        for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (up == "STRING") continue;                 // un texte ne s'anime pas
        ValueRow r;
        r.variable = mv.name;
        r.root = mv.root;
        r.address = mv.address;
        r.type = mv.type;
        r.table = mv.table;
        r.offset = mv.first;
        r.bit = mv.bit;
        r.boolean = zones::isBits(mv.table) || mv.bit >= 0 || isBoolType(mv.type);
        if (r.boolean) r.type = "BOOL";
        if (const auto* v = p.variable(mv.root); v && mv.name == mv.root && v->scaled()) {
            r.scaled = true;
            r.rawMin = v->rawMin;
            r.rawMax = v->rawMax;
            r.engMin = v->engMin;
            r.engMax = v->engMax;
        }
        r.written = mv.written();
        for (std::size_t i = 0; i < mv.writers.size() && i < 2; ++i) r.writers += (i ? ", " : "") + mv.writers[i];
        if (mv.writers.size() > 2) r.writers += ", ...";
        keys.push_back({r.table, r.offset, r.bit, true});
        out.push_back(std::move(r));
    }
    const auto find = [&](const CellKey& k) -> int {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == k) return static_cast<int>(i);
        return -1;
    };
    const auto extra = [&](const std::string& address, const std::string& type, const CellKey& k) {
        ValueRow r;
        r.address = address;
        r.table = k.table;
        r.offset = k.offset;
        r.bit = k.bit;
        r.boolean = zones::isBits(k.table) || k.bit >= 0 || isBoolType(type);
        r.type = r.boolean ? std::string("BOOL") : type;
        keys.push_back(k);
        out.push_back(std::move(r));
        return static_cast<int>(out.size() - 1);
    };
    for (std::size_t i = 0; i < e.behaviors.size(); ++i) {
        const auto& b = e.behaviors[i];
        const auto k = cellOf(b.address, b.type);
        if (!k.ok) continue;
        int at = find(k);
        if (at < 0) at = extra(b.address, b.type, k);
        if (out[static_cast<std::size_t>(at)].behavior < 0) out[static_cast<std::size_t>(at)].behavior = static_cast<int>(i);
    }
    for (std::size_t i = 0; i < e.forcings.size(); ++i) {
        const auto& f = e.forcings[i];
        const auto k = cellOf(f.address, f.type);
        if (!k.ok) continue;
        int at = find(k);
        if (at < 0) at = extra(f.address, f.type, k);
        if (out[static_cast<std::size_t>(at)].forcing < 0) out[static_cast<std::size_t>(at)].forcing = static_cast<int>(i);
    }
    return out;
}

std::optional<ValueRow> freeRow(const std::string& address, const std::string& type) {
    const auto k = cellOf(address, type);
    if (!k.ok) return std::nullopt;
    ValueRow r;
    r.address = address;
    r.table = k.table;
    r.offset = k.offset;
    r.bit = k.bit;
    r.boolean = zones::isBits(k.table) || k.bit >= 0 || isBoolType(type);
    r.type = r.boolean ? std::string("BOOL") : (type.empty() ? std::string("INT") : type);
    return r;
}

std::optional<double> rowValue(const TwinBank& bank, const ValueRow& r, bool low) {
    Behavior b;
    b.address = r.address;
    b.type = r.type;
    Place p;
    if (!placeOf(b, p, nullptr)) return std::nullopt;
    return readPlace(bank, p, r.type, low);
}

Behavior defaultBehavior(const ValueRow& r, std::optional<double> nowRaw) {
    Behavior b;
    b.address = r.address;
    b.type = r.boolean ? std::string("BOOL") : r.type;
    b.enabled = true;
    if (r.boolean) {
        b.kind = BehaviorKind::Blink;
        b.a = 0;
        b.b = 1;
        b.period = 2;
        return b;
    }
    b.kind = BehaviorKind::Sine;
    b.period = 30;
    double lo = 0, hi = 100;
    if (r.scaled) {
        const double span = r.rawMax - r.rawMin;
        lo = r.rawMin + span * 0.3;
        hi = r.rawMin + span * 0.7;
        if (lo > hi) std::swap(lo, hi);
    } else if (nowRaw && std::fabs(*nowRaw) > 1e-9) {
        const double d = std::max(1.0, std::fabs(*nowRaw) * 0.1);
        lo = *nowRaw - d;
        hi = *nowRaw + d;
    }
    if (unsignedType(b.type)) lo = std::max(0.0, lo);
    if (!isRealType(b.type)) {
        lo = std::round(lo);
        hi = std::round(hi);
        if (hi <= lo) hi = lo + 1;
    }
    b.a = lo;
    b.b = hi;
    return b;
}

std::pair<double, double> barRange(const ValueRow& r, const Behavior* b, std::optional<double> nowRaw) {
    if (r.boolean) return b && b->kind == BehaviorKind::Blink ? std::pair<double, double>{0, b->period > 0 ? b->period : 2} : std::pair<double, double>{0, 1};
    if (r.scaled) return {std::min(r.engMin, r.engMax), std::max(r.engMin, r.engMax)};
    double lo = 0, hi = 0;
    bool any = false;
    const auto take = [&](double v) {
        if (!any) {
            lo = hi = v;
            any = true;
        } else {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    };
    if (b && (b->kind == BehaviorKind::Sine || b->kind == BehaviorKind::Ramp || b->kind == BehaviorKind::Random)) {
        take(b->a);
        take(b->b);
    } else if (b && b->kind == BehaviorKind::Constant) {
        take(b->a);
    } else if (b && b->kind == BehaviorKind::Steps) {
        for (const double v : stepsOf(b->source)) take(v);
    }
    if (!any && nowRaw) {
        // Seulement la valeur : de 0 a une fois et demie la valeur (0 : 0 a 100).
        const double v = *nowRaw;
        if (std::fabs(v) < 1e-9) return {0, 100};
        return v > 0 ? niceAround(0, v * 1.2) : niceAround(v * 1.2, 0);
    }
    if (!any) return {0, 100};
    return niceAround(lo, hi);
}

bool parseRaw(const ValueRow& r, const std::string& text, double& out, std::string* why) {
    std::string v;
    for (char c : text)
        if (c != ' ') v.push_back(c);
    std::string l = v;
    for (auto& c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (r.boolean) {
        if (l == "1" || l == "true" || l == "vrai" || l == "on") out = 1;
        else if (l == "0" || l == "false" || l == "faux" || l == "off") out = 0;
        else {
            if (why) *why = "\xC2\xAB " + text + " \xC2\xBB : 0 ou 1";
            return false;
        }
        return true;
    }
    if (v.empty()) {
        if (why) *why = "une valeur brute (5100, 0x1F, 12.5)";
        return false;
    }
    double d = 0;
    char* end = nullptr;
    if (l.size() > 2 && l[0] == '0' && l[1] == 'x') {
        d = static_cast<double>(std::strtoll(l.c_str(), &end, 16));
    } else {
        std::replace(v.begin(), v.end(), ',', '.');
        d = std::strtod(v.c_str(), &end);
    }
    if (!end || *end || !std::isfinite(d)) {
        if (why) *why = "\xC2\xAB " + text + " \xC2\xBB : un nombre (5100, -5, 0x1F, 12.5)";
        return false;
    }
    std::string t = r.type;
    for (auto& c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto range = [&](double lo, double hi) {
        if (d < lo || d > hi || std::fabs(d - std::round(d)) > 1e-9) {
            if (why) *why = text + " : un " + t + " va de " + num(lo) + " \xC3\xA0 " + num(hi) + " (un entier)";
            return false;
        }
        return true;
    };
    if (t == "REAL" || t == "LREAL") {
        out = d;
        return true;
    }
    if (t == "UINT" || t == "WORD") {
        if (!range(0, 65535)) return false;
    } else if (t == "DINT") {
        if (!range(-2147483648.0, 2147483647.0)) return false;
    } else if (t == "UDINT" || t == "DWORD" || t == "TIME") {
        if (!range(0, 4294967295.0)) return false;
    } else if (!range(-32768, 32767)) {
        return false;
    }
    out = d;
    return true;
}

std::string rowForcingText(const ValueRow& r, double raw) {
    if (r.boolean) return raw != 0 ? "1" : "0";
    if (r.scaled) return num(raw) + " (= " + num(r.eng(raw)) + ")";
    return num(raw);
}

std::string numberText(double v) { return num(v); }

// ------------------------------------------------------------------ fichiers ---
std::string exportCsv(const TwinBank& bank) {
    std::string out = "table;adresse;modicon;valeur\n";
    for (const auto& s : bank.snapshot())
        for (std::size_t i = 0; i < s.values.size(); ++i) {
            if (s.values[i] == 0) continue;
            const auto a = s.first + static_cast<std::uint32_t>(i);
            out += std::string(zones::tableModicon(s.table)) + ";" + std::to_string(a) + ";" + zones::modicon(s.table, a) + ";" + std::to_string(s.values[i]) + "\n";
        }
    return out;
}

bool importCsv(TwinBank& bank, std::string_view csv, std::string* why, std::size_t* count) {
    std::size_t n = 0, lineNo = 0;
    std::size_t pos = 0;
    while (pos < csv.size()) {
        std::size_t end = csv.find('\n', pos);
        if (end == std::string_view::npos) end = csv.size();
        std::string line(csv.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || (lineNo == 1 && line.rfind("table", 0) == 0)) continue;   // l'en-tete
        std::vector<std::string> f;
        std::string cur;
        for (char c : line) {
            if (c == ';' || c == ',') {
                f.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        f.push_back(cur);
        if (f.size() < 3) {
            if (why) *why = "ligne " + std::to_string(lineNo) + " : table;adresse;valeur attendus";
            return false;
        }
        const auto t = zones::tableFrom(f[0]);
        const std::string& value = f.size() >= 4 ? f[3] : f[2];
        if (!t) {
            if (why) *why = "ligne " + std::to_string(lineNo) + " : table \xC2\xAB " + f[0] + " \xC2\xBB inconnue (0x, 1x, 3x, 4x)";
            return false;
        }
        try {
            const unsigned long a = std::stoul(f[1]);
            const long v = std::stol(value);
            if (a >= kSpace) throw std::out_of_range("adresse");
            bank.setWord(*t, static_cast<std::uint32_t>(a), static_cast<std::uint16_t>(v & 0xFFFF));
            ++n;
        } catch (...) {
            if (why) *why = "ligne " + std::to_string(lineNo) + " : adresse ou valeur illisible";
            return false;
        }
    }
    if (count) *count = n;
    return true;
}

} // namespace hmi::twin
