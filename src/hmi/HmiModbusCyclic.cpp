// hmi/HmiModbusCyclic.cpp - la lecture cyclique a plusieurs requetes (1.9) :
// les valeurs, le regroupement des variables, le moteur (un fil par cible),
// le CSV et l'enregistrement continu.
#include "HmiModbusCyclic.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace hmi::mbtool {

namespace {

namespace fs = std::filesystem;

constexpr double kKeepSeconds = 900.0;          // l'historique : un quart d'heure
constexpr std::size_t kKeepSamples = 20000;     // ... et 20 000 lectures au plus par requete
constexpr std::size_t kJournalMax = 2000;

double wallNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
double monoNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

std::uint32_t join32(std::uint16_t first, std::uint16_t second, bool lowFirst) {
    return lowFirst ? (static_cast<std::uint32_t>(second) << 16) | first : (static_cast<std::uint32_t>(first) << 16) | second;
}

bool bitFunction(int f) { return f == 1 || f == 2; }

// Le temps local d'une heure murale.
std::tm localOf(double wall) {
    const auto secs = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    return tm;
}

// Un nom de fichier : minuscules, tirets, sans accents ("Mise en service armoire A"
// -> "mise-en-service-armoire-a").
std::string fileStem(std::string_view name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        char put = 0;
        if (c < 0x80) {
            if (std::isalnum(c)) put = static_cast<char>(std::tolower(c));
            else if (c == '-' || c == '_' || c == ' ' || c == '.' || c == '\'') put = '-';
        } else if (c == 0xC3 && i + 1 < name.size()) {
            // Le Latin-1 accentue (U+00C0 a U+00FF) : sa lettre de base.
            static const char* base = "aaaaaaaceeeeiiiidnooooo-ouuuuyps"
                                      "aaaaaaaceeeeiiiidnooooo-ouuuuypy";
            const unsigned char d = static_cast<unsigned char>(name[i + 1]);
            if (d >= 0x80 && d <= 0xBF) put = base[d - 0x80];
            ++i;
        } else {
            // Un autre caractere : passe (ses octets de suite aussi).
            while (i + 1 < name.size() && (static_cast<unsigned char>(name[i + 1]) & 0xC0) == 0x80) ++i;
            put = '-';
        }
        if (!put) continue;
        if (put == '-' && (out.empty() || out.back() == '-')) continue;
        out += put;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "lecture";
    if (out.size() > 60) out.resize(60);
    return out;
}

fs::path pathOf(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

// La meme lecture ? (la cible, la table, la place, les valeurs - pas le nom ni la periode)
bool sameReading(const CyclicRequest& a, const CyclicRequest& b) {
    if (!(a.target == b.target) || a.function != b.function || a.address != b.address || a.count != b.count || a.lowFirst != b.lowFirst)
        return false;
    if (a.values.size() != b.values.size()) return false;
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        const auto& x = a.values[i];
        const auto& y = b.values[i];
        if (x.kind != y.kind || x.format != y.format || x.offset != y.offset || x.words != y.words || x.bit != y.bit || x.scaled != y.scaled
            || x.rawMin != y.rawMin || x.rawMax != y.rawMax || x.scaleMin != y.scaleMin || x.scaleMax != y.scaleMax)
            return false;
    }
    return true;
}

bool numericKind(const ValueSpec& v) { return v.kind == ValueKind::Number || v.kind == ValueKind::Bit; }

} // namespace

// ================================================================== valeurs ===
ValueSpec specForType(std::string name, std::string_view type, int offset, int bit, int words) {
    ValueSpec v;
    v.name = std::move(name);
    v.offset = offset;
    const std::string t = upperOf(type);
    v.type = t;
    const auto number = [&](Format f, int w) {
        v.kind = ValueKind::Number;
        v.format = f;
        v.words = w;
    };
    if (t == "BOOL" || t == "EBOOL") {
        v.kind = ValueKind::Bit;
        v.bit = bit;
        v.words = 1;
    } else if (t == "INT") {
        number(Format::Signed, 1);
    } else if (t == "UINT" || t == "WORD" || t == "BYTE") {
        number(Format::Unsigned, 1);
    } else if (t == "DINT" || t == "TIME") {
        number(Format::Signed32, 2);
    } else if (t == "UDINT" || t == "DWORD") {
        number(Format::Unsigned32, 2);
    } else if (t == "REAL") {
        number(Format::Float32, 2);
    } else if (t.rfind("STRING", 0) == 0) {
        v.kind = ValueKind::Text;
        v.words = words > 0 ? words : 8;
    } else if (words > 2) {
        v.kind = ValueKind::Text;
        v.words = words;
    } else if (words == 2) {
        number(Format::Unsigned32, 2);
    } else {
        number(Format::Unsigned, 1);
    }
    return v;
}

std::vector<ValueSpec> valuesForFormat(int function, int count, Format format, const std::function<std::string(int)>& names) {
    std::vector<ValueSpec> out;
    const auto nameAt = [&](int offset) { return names ? names(offset) : std::string{}; };
    count = std::max(0, count);
    if (bitFunction(function)) {
        // Un bit nomme : sa ligne ; les autres, en bandes de 16 au plus.
        int stripStart = -1;
        const auto flush = [&](int end) {
            if (stripStart < 0) return;
            ValueSpec v;
            if (end - stripStart == 1) {
                v.kind = ValueKind::Bit;
                v.words = 1;
            } else {
                v.kind = ValueKind::Bits;
                v.words = end - stripStart;
            }
            v.offset = stripStart;
            out.push_back(std::move(v));
            stripStart = -1;
        };
        for (int i = 0; i < count; ++i) {
            std::string name = nameAt(i);
            if (!name.empty()) {
                flush(i);
                ValueSpec v;
                v.kind = ValueKind::Bit;
                v.offset = i;
                v.words = 1;
                v.name = std::move(name);
                v.type = "BOOL";
                out.push_back(std::move(v));
                continue;
            }
            if (stripStart < 0) stripStart = i;
            else if (i - stripStart >= 16) {
                flush(i);
                stripStart = i;
            }
        }
        flush(count);
        return out;
    }
    if (format == Format::Ascii) {
        ValueSpec v;
        v.kind = ValueKind::Text;
        v.format = Format::Ascii;
        v.offset = 0;
        v.words = count;
        v.name = nameAt(0);
        out.push_back(std::move(v));
        return out;
    }
    const int step = wide(format) ? 2 : 1;
    int i = 0;
    for (; i + step <= count; i += step) {
        ValueSpec v;
        v.kind = ValueKind::Number;
        v.format = format;
        v.offset = i;
        v.words = step;
        v.name = nameAt(i);
        out.push_back(std::move(v));
    }
    if (i < count) {                                  // un mot de reste (32 bits sur un nombre impair)
        ValueSpec v;
        v.kind = ValueKind::Number;
        v.format = Format::Unsigned;
        v.offset = i;
        v.words = 1;
        v.name = nameAt(i);
        out.push_back(std::move(v));
    }
    return out;
}

Decoded decodeValue(const ValueSpec& v, const std::vector<std::uint16_t>& regs, const std::vector<bool>& bits, bool lowFirst) {
    Decoded d;
    if (v.offset < 0) return d;
    const auto at = static_cast<std::size_t>(v.offset);
    switch (v.kind) {
        case ValueKind::Bit: {
            if (v.bit >= 0) {
                if (at >= regs.size()) return d;
                const bool b = ((regs[at] >> std::clamp(v.bit, 0, 15)) & 1u) != 0;
                d.number = b ? 1 : 0;
            } else {
                if (at < bits.size()) d.number = bits[at] ? 1 : 0;
                else if (at < regs.size()) d.number = regs[at] != 0 ? 1 : 0;     // un BOOL dans un mot
                else return d;
            }
            d.raw = {static_cast<std::uint16_t>(d.number > 0.5 ? 1 : 0)};
            d.ok = true;
            return d;
        }
        case ValueKind::Bits: {
            const auto n = static_cast<std::size_t>(std::max(1, v.words));
            if (at + n > bits.size()) return d;
            for (std::size_t k = 0; k < n; ++k) {
                const bool b = bits[at + k];
                d.text += b ? '1' : '0';
                d.raw.push_back(b ? 1 : 0);
            }
            d.ok = true;
            return d;
        }
        case ValueKind::Text: {
            const auto n = static_cast<std::size_t>(std::max(1, v.words));
            if (at + n > regs.size()) return d;
            bool ended = false;
            for (std::size_t k = 0; k < n; ++k) {
                const std::uint16_t w = regs[at + k];
                d.raw.push_back(w);
                const int first = lowFirst ? (w & 0xFF) : (w >> 8);
                const int second = lowFirst ? (w >> 8) : (w & 0xFF);
                for (const int c : {first, second}) {
                    if (ended) break;
                    if (c == 0) { ended = true; break; }
                    d.text += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
                }
            }
            while (!d.text.empty() && d.text.back() == ' ') d.text.pop_back();
            d.ok = true;
            return d;
        }
        case ValueKind::Number: break;
    }
    const bool two = wide(v.format) || v.words == 2;
    if (at >= regs.size() || (two && at + 1 >= regs.size())) return d;
    double x = 0;
    if (!two) {
        const std::uint16_t w = regs[at];
        d.raw = {w};
        x = v.format == Format::Signed ? static_cast<double>(static_cast<std::int16_t>(w)) : static_cast<double>(w);
    } else {
        d.raw = {regs[at], regs[at + 1]};
        const std::uint32_t u = join32(regs[at], regs[at + 1], lowFirst);
        if (v.format == Format::Signed32) x = static_cast<std::int32_t>(u);
        else if (v.format == Format::Float32) {
            float f = 0;
            std::memcpy(&f, &u, sizeof f);
            x = static_cast<double>(f);
        } else {
            x = static_cast<double>(u);
        }
    }
    if (v.scaled && std::fabs(v.rawMax - v.rawMin) > 1e-12)
        x = v.scaleMin + (x - v.rawMin) * (v.scaleMax - v.scaleMin) / (v.rawMax - v.rawMin);
    d.number = x;
    d.ok = std::isfinite(x);
    if (!d.ok) d.text = "NaN";
    return d;
}

std::string frenchNumber(double v, int decimals, bool group) {
    if (!std::isfinite(v)) return "\xE2\x80\x94";
    if (decimals < 0) {
        const double r = std::round(v);
        if (std::fabs(v - r) < 1e-9 && std::fabs(r) < 1e15) {
            decimals = 0;
        } else {
            const double mag = std::fabs(v) > 0 ? std::floor(std::log10(std::fabs(v))) : 0;
            decimals = static_cast<int>(std::clamp(6.0 - mag, 0.0, 9.0));
        }
    }
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", std::clamp(decimals, 0, 12), v);
    std::string s = b;
    // Les zeros inutiles d'un nombre automatique.
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    std::string sign;
    if (!s.empty() && s.front() == '-') {
        sign = "-";
        s.erase(s.begin());
    }
    const auto dot = s.find('.');
    std::string ip = dot == std::string::npos ? s : s.substr(0, dot);
    const std::string fp = dot == std::string::npos ? std::string{} : s.substr(dot + 1);
    if (group && ip.size() > 3) {
        std::string g;
        for (std::size_t i = 0; i < ip.size(); ++i) {
            if (i && (ip.size() - i) % 3 == 0) g += "\xE2\x80\xAF";
            g += ip[i];
        }
        ip = g;
    }
    return sign + ip + (fp.empty() ? std::string{} : "," + fp);
}

namespace {

std::string hexText(double number, int words) {
    char b[24];
    const auto u = static_cast<std::uint32_t>(static_cast<std::int64_t>(number) & 0xFFFFFFFFLL);
    if (words >= 2) std::snprintf(b, sizeof b, "0x%08X", u);
    else std::snprintf(b, sizeof b, "0x%04X", u & 0xFFFFu);
    return b;
}

std::string binaryText(double number, int words) {
    const auto u = static_cast<std::uint32_t>(static_cast<std::int64_t>(number) & 0xFFFFFFFFLL);
    const int n = words >= 2 ? 32 : 16;
    std::string s;
    for (int k = n - 1; k >= 0; --k) {
        s += ((u >> k) & 1u) ? '1' : '0';
        if (k % 4 == 0 && k) s += ' ';
    }
    return s;
}

} // namespace

std::string valueText(const ValueSpec& v, double number, const std::string& text, bool unit) {
    switch (v.kind) {
        case ValueKind::Text: return "\xC2\xAB " + text + " \xC2\xBB";
        case ValueKind::Bits: return text;
        case ValueKind::Bit: return std::isfinite(number) ? (number > 0.5 ? "1" : "0") : "\xE2\x80\x94";
        case ValueKind::Number: break;
    }
    if (!std::isfinite(number)) return "\xE2\x80\x94";
    std::string out;
    if (!v.scaled && v.format == Format::Hex) out = hexText(number, v.words);
    else if (!v.scaled && v.format == Format::Binary) out = binaryText(number, v.words);
    else if (v.scaled || v.format == Format::Float32) out = frenchNumber(number, -1);
    else out = frenchNumber(number, 0);
    if (unit && !v.unit.empty()) out += " " + v.unit;
    return out;
}

std::string csvValue(const ValueSpec& v, double number, const std::string& text) {
    switch (v.kind) {
        case ValueKind::Text: return text;
        case ValueKind::Bits: return text;
        case ValueKind::Bit: return std::isfinite(number) ? (number > 0.5 ? "1" : "0") : std::string{};
        case ValueKind::Number: break;
    }
    if (!std::isfinite(number)) return {};
    if (!v.scaled && v.format == Format::Hex) return hexText(number, v.words);
    if (!v.scaled && v.format == Format::Binary) return binaryText(number, v.words);
    return frenchNumber(number, v.scaled || v.format == Format::Float32 ? -1 : 0, false);
}

// ================================================================= requetes ===
bool validRequest(const CyclicRequest& r, std::string* why) {
    if (r.function < 1 || r.function > 4) return fail(why, "fonction " + std::to_string(r.function) + " : 1, 2, 3 ou 4 (lire)");
    if (r.address < 0 || r.address > 65535) return fail(why, "adresse hors de 0 \xC3\xA0 65535");
    if (bitFunction(r.function)) {
        if (r.count < 1 || r.count > modbus::kMaxReadBits) return fail(why, "de 1 \xC3\xA0 2000 bits par requ\xC3\xAAte");
    } else if (r.count < 1 || r.count > modbus::kMaxReadRegisters) {
        return fail(why, "de 1 \xC3\xA0 125 mots par requ\xC3\xAAte");
    }
    if (r.address + r.count > 65536) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
    for (const auto& v : r.values) {
        const int size = v.kind == ValueKind::Bit && v.bit < 0 ? 1 : std::max(1, v.words);
        if (v.offset < 0 || v.offset + (v.kind == ValueKind::Bit ? 1 : size) > r.count)
            return fail(why, (v.name.empty() ? std::string("une valeur") : v.name) + " : hors de la requ\xC3\xAAte");
    }
    return true;
}

std::string targetKey(const Target& t) { return t.host + ":" + std::to_string(t.port) + ":" + std::to_string(t.unit); }

std::string failureText(const Reply& r, int timeoutMs) {
    if (r.exception) {
        char code[8];
        std::snprintf(code, sizeof code, "%02X", r.exception & 0xFF);
        const std::string head = std::string("exception ") + code + " : ";
        switch (r.exception) {
            case 1: return head + "fonction non prise en charge \xE2\x80\x94 l'appareil ne conna\xC3\xAEt pas cette fonction";
            case 2: return head + "adresse ill\xC3\xA9gale \xE2\x80\x94 l'appareil n'a pas ces registres";
            case 3: return head + "valeur ill\xC3\xA9gale \xE2\x80\x94 l'appareil refuse ce nombre de valeurs";
            case 4: return head + "d\xC3\xA9" "faut de l'appareil";
            case 5: return head + "requ\xC3\xAAte accept\xC3\xA9" "e, traitement long";
            case 6: return head + "appareil occup\xC3\xA9 \xE2\x80\x94 il r\xC3\xA9pondra plus tard";
            case 10: return head + "passerelle sans chemin vers l'appareil";
            case 11: return head + "l'appareil derri\xC3\xA8re la passerelle ne r\xC3\xA9pond pas";
            default: return head + "exception inconnue";
        }
    }
    const bool silent = r.timeout || r.why.find("pas de r\xC3\xA9ponse") != std::string::npos || r.why.find("d\xC3\xA9lai") != std::string::npos;
    if (silent)
        return "d\xC3\xA9lai d\xC3\xA9pass\xC3\xA9 : pas de r\xC3\xA9ponse" + (timeoutMs > 0 ? " en " + std::to_string(timeoutMs) + " ms" : std::string{});
    if (r.why.empty()) return "pas de r\xC3\xA9ponse";
    return "connexion impossible : " + r.why;
}

std::string failureShort(const Reply& r) {
    if (r.exception) {
        char code[24];
        std::snprintf(code, sizeof code, "exception %02X", r.exception & 0xFF);
        return code;
    }
    const bool silent = r.timeout || r.why.find("pas de r\xC3\xA9ponse") != std::string::npos || r.why.find("d\xC3\xA9lai") != std::string::npos;
    if (silent || r.why.empty()) return "pas de r\xC3\xA9ponse";
    return "connexion impossible";
}

// ======================================================= la lecture cyclique ===
MultiPoller::MultiPoller() = default;

MultiPoller::~MultiPoller() {
    stopRecording();
    stop();
    std::vector<Worker> all;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        all = std::move(retired_);
        retired_.clear();
    }
    for (auto& w : all)
        if (w.thread.joinable()) w.thread.join();
}

MultiPoller::Entry* MultiPoller::entry(int id) {
    for (auto& e : entries_)
        if (e.request.id == id) return &e;
    return nullptr;
}

const MultiPoller::Entry* MultiPoller::entry(int id) const {
    for (const auto& e : entries_)
        if (e.request.id == id) return &e;
    return nullptr;
}

void MultiPoller::configure(std::vector<CyclicRequest> requests, CyclicOptions options) {
    reapRetired();
    std::lock_guard<std::mutex> lock(mutex_);
    options.periodMs = std::clamp(options.periodMs, 20, 3600000);
    options.maxFailures = std::max(0, options.maxFailures);
    options.pauseMs = std::clamp(options.pauseMs, 0, 60000);
    const bool chainChanged = options.perTarget != options_.perTarget;
    options_ = options;
    const double now = monoNow();
    std::vector<Entry> next;
    next.reserve(requests.size());
    for (auto& r : requests) {
        Entry* old = entry(r.id);
        if (old && sameReading(old->request, r)) {
            Entry e = std::move(*old);
            old->request.id = -1;                         // pris
            const bool wake = r.active && !e.request.active;
            const int period = r.periodMs;
            if (period != e.request.periodMs || wake) e.nextDue = now;
            e.request = std::move(r);
            e.state.active = e.request.active;
            next.push_back(std::move(e));
            continue;
        }
        Entry e;
        e.request = std::move(r);
        e.generation = ++generations_;
        e.state.id = e.request.id;
        e.state.active = e.request.active;
        e.state.values.resize(e.request.values.size());
        e.nextDue = now;
        next.push_back(std::move(e));
    }
    entries_ = std::move(next);
    if (running_.load()) {
        if (chainChanged)
            for (auto& w : workers_) {
                w.stop->store(true);
                retired_.push_back(std::move(w));
            }
        if (chainChanged) workers_.clear();
        syncWorkers();
    }
    wake_.notify_all();
}

std::vector<CyclicRequest> MultiPoller::requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CyclicRequest> out;
    for (const auto& e : entries_) out.push_back(e.request);
    return out;
}

CyclicOptions MultiPoller::options() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return options_;
}

void MultiPoller::syncWorkers() {
    std::set<std::string> wanted;
    if (!options_.perTarget) wanted.insert(std::string{});
    else
        for (const auto& e : entries_) wanted.insert(targetKey(e.request.target));
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (!wanted.count(it->key)) {
            it->stop->store(true);
            retired_.push_back(std::move(*it));
            it = workers_.erase(it);
        } else {
            wanted.erase(it->key);
            ++it;
        }
    }
    for (const auto& key : wanted) {
        Worker w;
        w.key = key;
        w.stop = std::make_shared<std::atomic<bool>>(false);
        w.done = std::make_shared<std::atomic<bool>>(false);
        auto stop = w.stop;
        auto done = w.done;
        w.thread = std::thread([this, key, stop, done] {
            work(key, stop);
            done->store(true);
        });
        workers_.push_back(std::move(w));
    }
}

void MultiPoller::reapRetired() {
    // Les fils arretes qui ont fini : joints tout de suite (les autres finissent
    // leur requete en cours - un delai au plus - et seront joints plus tard).
    std::vector<Worker> done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = retired_.begin(); it != retired_.end();) {
            if (it->done->load()) {
                done.push_back(std::move(*it));
                it = retired_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& w : done)
        if (w.thread.joinable()) w.thread.join();
}

void MultiPoller::start() {
    reapRetired();
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) return;
    running_ = true;
    const double now = monoNow();
    for (auto& e : entries_) e.nextDue = now;
    syncWorkers();
    wake_.notify_all();
}

void MultiPoller::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        for (auto& w : workers_) {
            w.stop->store(true);
            retired_.push_back(std::move(w));
        }
        workers_.clear();
    }
    wake_.notify_all();
    reapRetired();
}

bool MultiPoller::setActive(int id, bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* e = entry(id);
    if (!e) return false;
    if (on && !e->request.active) e->nextDue = monoNow();
    e->request.active = on;
    e->state.active = on;
    if (on) {
        e->state.paused = false;
        e->state.failuresInRow = 0;
    }
    wake_.notify_all();
    return true;
}

bool MultiPoller::resume(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* e = entry(id);
    if (!e) return false;
    e->state.paused = false;
    e->state.failuresInRow = 0;
    e->nextDue = monoNow();
    wake_.notify_all();
    return true;
}

void MultiPoller::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        RequestState fresh;
        fresh.id = e.request.id;
        fresh.active = e.request.active;
        fresh.paused = e.state.paused;
        fresh.values.resize(e.request.values.size());
        e.state = std::move(fresh);
        e.samples.clear();
        e.sumMs = 0;
        e.generation = ++generations_;    // une lecture en cours ne compte plus
        e.busy = false;
    }
    journal_.clear();
}

void MultiPoller::work(std::string key, std::shared_ptr<std::atomic<bool>> stop) {
    std::map<std::string, std::unique_ptr<Session>> sessions;
    while (!stop->load()) {
        int id = 0;
        std::uint64_t generation = 0;
        Query q;
        Target t;
        int pauseMs = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop->load()) break;
            const double now = monoNow();
            Entry* best = nullptr;
            for (auto& e : entries_) {
                if (!e.request.active || e.state.paused || e.busy) continue;
                if (!key.empty() && targetKey(e.request.target) != key) continue;
                if (!best || e.nextDue < best->nextDue) best = &e;
            }
            if (!best || best->nextDue > now) {
                const double wait = best ? std::clamp(best->nextDue - now, 0.001, 0.05) : 0.05;
                wake_.wait_for(lock, std::chrono::duration<double>(wait), [&] { return stop->load(); });
                continue;
            }
            best->busy = true;
            id = best->request.id;
            generation = best->generation;
            q.function = best->request.function;
            q.address = best->request.address;
            q.count = best->request.count;
            t = best->request.target;
            pauseMs = options_.pauseMs;
        }
        auto& s = sessions[targetKey(t)];
        if (!s) s = std::make_unique<Session>(t);
        else s->setTarget(t);
        const Reply r = s->run(q);
        record(id, generation, r, wallNow());
        if (pauseMs > 0) {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(pauseMs), [&] { return stop->load(); });
        }
    }
    for (auto& [k, s] : sessions)
        if (s) s->close();
}

void MultiPoller::record(int id, std::uint64_t generation, const Reply& r, double wall) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry* e = entry(id);
    if (!e || e->generation != generation) return;       // retiree, changee ou effacee entre-temps
    e->busy = false;
    auto& st = e->state;
    const double now = monoNow();
    const int period = e->request.periodMs > 0 ? e->request.periodMs : options_.periodMs;
    e->nextDue += std::max(0.02, period / 1000.0);
    if (e->nextDue < now) e->nextDue = now;              // en retard (un delai depasse) : on repart de maintenant
    ++st.reads;
    st.lastAt = wall;
    std::string text;
    if (r.ok) {
        st.lastOk = true;
        st.everOk = true;
        st.failuresInRow = 0;
        st.exception = 0;
        st.timeout = false;
        st.lastMs = r.ms;
        e->sumMs += r.ms;
        const auto good = st.reads - st.errors;
        st.avgMs = good ? e->sumMs / static_cast<double>(good) : r.ms;
        st.maxMs = std::max(st.maxMs, r.ms);
        st.values.resize(e->request.values.size());
        std::size_t shown = 0;
        for (std::size_t i = 0; i < e->request.values.size(); ++i) {
            const auto& spec = e->request.values[i];
            const Decoded d = decodeValue(spec, r.registers, r.bits, e->request.lowFirst);
            auto& v = st.values[i];
            if (!d.ok) continue;
            if (v.has && (d.raw != v.raw || d.text != v.text)) {
                ++v.changes;
                v.changedAt = wall;
            }
            if (!v.has) v.changedAt = wall;
            v.has = true;
            v.number = d.number;
            v.text = d.text;
            v.raw = d.raw;
            if (numericKind(spec) && std::isfinite(d.number)) {
                if (v.count == 0) v.min = v.max = d.number;
                v.min = std::min(v.min, d.number);
                v.max = std::max(v.max, d.number);
                v.sum += d.number;
                ++v.count;
            }
            if (shown < 12) {
                text += (shown ? "  " : "") + valueText(spec, d.number, d.text, false);
                ++shown;
            }
        }
        if (e->request.values.size() > 12) text += "  \xE2\x80\xA6";
    } else {
        ++st.errors;
        st.lastOk = false;
        ++st.failuresInRow;
        st.exception = r.exception;
        st.timeout = r.timeout;
        st.lastError = failureText(r, e->request.target.timeoutMs);
        st.lastErrorShort = failureShort(r);
        if (options_.maxFailures > 0 && st.failuresInRow >= options_.maxFailures) st.paused = true;
        text = st.lastError;
    }
    Sample smp;
    smp.t = wall;
    smp.ok = r.ok;
    smp.ms = r.ms;
    if (r.ok) {
        smp.registers = r.registers;
        smp.bits = r.bits;
    }
    e->samples.push_back(std::move(smp));
    while (!e->samples.empty() && (e->samples.size() > kKeepSamples || e->samples.front().t < wall - kKeepSeconds)) e->samples.pop_front();
    journal_.push_front({wall, id, r.ok, r.ms, std::move(text)});
    while (journal_.size() > kJournalMax) journal_.pop_back();
}

std::vector<MultiPoller::RequestState> MultiPoller::states() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RequestState> out;
    out.reserve(entries_.size());
    for (const auto& e : entries_) out.push_back(e.state);
    return out;
}

std::optional<MultiPoller::RequestState> MultiPoller::state(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Entry* e = entry(id);
    if (!e) return std::nullopt;
    return e->state;
}

std::vector<MultiPoller::JournalEntry> MultiPoller::journal(std::size_t max, int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<JournalEntry> out;
    for (const auto& j : journal_) {
        if (out.size() >= max) break;
        if (id > 0 && j.id != id) continue;
        out.push_back(j);
    }
    return out;
}

std::size_t MultiPoller::journalSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return journal_.size();
}

std::vector<std::pair<double, double>> MultiPoller::history(int id, std::size_t value, double seconds, double until) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<double, double>> out;
    const Entry* e = entry(id);
    if (!e || value >= e->request.values.size()) return out;
    const double to = until > 0 ? until : wallNow();
    const double from = to - seconds;
    const auto& spec = e->request.values[value];
    for (const auto& s : e->samples) {
        if (!s.ok || s.t < from || s.t > to) continue;
        const Decoded d = decodeValue(spec, s.registers, s.bits, e->request.lowFirst);
        if (d.ok && std::isfinite(d.number)) out.emplace_back(s.t, d.number);
    }
    return out;
}

std::size_t MultiPoller::samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto& e : entries_) n += e.samples.size();
    return n;
}

// ======================================================================= CSV ===
std::string csvCell(std::string_view text) {
    const bool quote = text.find_first_of(";\"\n\r") != std::string_view::npos;
    if (!quote) return std::string(text);
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out + "\"";
}

std::string clockText(double wall, bool millis) {
    const std::tm tm = localOf(wall);
    char b[40];
    if (millis)
        std::snprintf(b, sizeof b, "%02d:%02d:%02d,%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      std::clamp(static_cast<int>(std::fmod(wall, 1.0) * 1000.0), 0, 999));
    else
        std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

std::string dateText(double wall, bool iso) {
    const std::tm tm = localOf(wall);
    char b[40];
    if (iso) std::snprintf(b, sizeof b, "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    else std::snprintf(b, sizeof b, "%02d/%02d/%04d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return b;
}

std::string MultiPoller::columnTitle(const CyclicRequest& r, const ValueSpec& v) {
    std::string place;
    if (!v.name.empty()) place = v.name;
    else {
        place = modiconOf(r.function, r.address + v.offset);
        if (v.kind == ValueKind::Bit && v.bit >= 0) place += "." + std::to_string(v.bit);
        if (v.kind == ValueKind::Bits) place += "\xE2\x80\xA6" + modiconOf(r.function, r.address + v.offset + v.words - 1);
    }
    std::string out = "R" + std::to_string(r.id) + " " + place;
    if (!v.unit.empty()) out += " (" + v.unit + ")";
    return out;
}

namespace {

// Les colonnes brutes d'une requete : un registre (ou un bit) chacune.
std::vector<std::string> rawTitles(const CyclicRequest& r) {
    std::vector<std::string> out;
    for (int k = 0; k < r.count; ++k) out.push_back("R" + std::to_string(r.id) + " " + modiconOf(r.function, r.address + k));
    return out;
}

void rawCells(const CyclicRequest& r, const std::vector<std::uint16_t>& regs, const std::vector<bool>& bits, bool have,
              std::string& line) {
    for (int k = 0; k < r.count; ++k) {
        line += ';';
        if (!have) continue;
        const auto at = static_cast<std::size_t>(k);
        if (bitFunction(r.function)) {
            if (at < bits.size()) line += bits[at] ? '1' : '0';
        } else if (at < regs.size()) {
            line += std::to_string(regs[at]);
        }
    }
}

void valueCells(const CyclicRequest& r, const std::vector<std::uint16_t>& regs, const std::vector<bool>& bits, bool have,
                std::string& line) {
    for (const auto& spec : r.values) {
        line += ';';
        if (!have) continue;
        const Decoded d = decodeValue(spec, regs, bits, r.lowFirst);
        if (d.ok) line += csvCell(csvValue(spec, d.number, d.text));
    }
}

} // namespace

std::string MultiPoller::csvAll(bool raw, int stepMs, std::size_t maxLines, std::size_t* lines) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out = "\xEF\xBB\xBF" "date;heure";
    for (const auto& e : entries_) {
        if (raw)
            for (const auto& t : rawTitles(e.request)) out += ";" + csvCell(t);
        else
            for (const auto& v : e.request.values) out += ";" + csvCell(columnTitle(e.request, v));
    }
    out += "\n";
    double t0 = 0, t1 = 0;
    bool any = false;
    for (const auto& e : entries_)
        for (const auto& s : e.samples) {
            if (!s.ok) continue;
            if (!any) t0 = t1 = s.t;
            t0 = std::min(t0, s.t);
            t1 = std::max(t1, s.t);
            any = true;
        }
    std::size_t made = 0;
    if (any) {
        const double step = std::max(0.02, (stepMs > 0 ? stepMs : options_.periodMs) / 1000.0);
        const auto total = static_cast<std::size_t>(std::floor((t1 - t0) / step + 1e-9)) + 1;
        std::size_t first = 0;
        if (maxLines > 0 && total > maxLines) first = total - maxLines;
        // Un curseur par requete : la derniere lecture bonne a l'heure de la ligne.
        std::vector<std::size_t> cursor(entries_.size(), 0);
        std::vector<const Sample*> lastGood(entries_.size(), nullptr);
        for (std::size_t k = 0; k < total; ++k) {
            const double t = t0 + static_cast<double>(k) * step;
            for (std::size_t i = 0; i < entries_.size(); ++i) {
                const auto& s = entries_[i].samples;
                while (cursor[i] < s.size() && s[cursor[i]].t <= t + 1e-6) {
                    if (s[cursor[i]].ok) lastGood[i] = &s[cursor[i]];
                    ++cursor[i];
                }
            }
            if (k < first) continue;
            std::string line = dateText(t) + ";" + clockText(t);
            for (std::size_t i = 0; i < entries_.size(); ++i) {
                const Sample* s = lastGood[i];
                static const std::vector<std::uint16_t> noRegs;
                static const std::vector<bool> noBits;
                if (raw) rawCells(entries_[i].request, s ? s->registers : noRegs, s ? s->bits : noBits, s != nullptr, line);
                else valueCells(entries_[i].request, s ? s->registers : noRegs, s ? s->bits : noBits, s != nullptr, line);
            }
            out += line + "\n";
            ++made;
        }
    }
    if (lines) *lines = made;
    return out;
}

std::vector<std::pair<int, std::string>> MultiPoller::csvEach(bool raw, std::size_t maxLines) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<int, std::string>> out;
    for (const auto& e : entries_) {
        std::string text = "\xEF\xBB\xBF" "date;heure;ms;etat";
        if (raw)
            for (const auto& t : rawTitles(e.request)) text += ";" + csvCell(t);
        else
            for (const auto& v : e.request.values) text += ";" + csvCell(columnTitle(e.request, v));
        text += "\n";
        std::size_t first = 0;
        if (maxLines > 0 && e.samples.size() > maxLines) first = e.samples.size() - maxLines;
        for (std::size_t k = first; k < e.samples.size(); ++k) {
            const auto& s = e.samples[k];
            std::string line = dateText(s.t) + ";" + clockText(s.t) + ";" + frenchNumber(s.ms, 1, false) + ";" + (s.ok ? "bonne" : "\xC3\xA9" "chec");
            if (raw) rawCells(e.request, s.registers, s.bits, s.ok, line);
            else valueCells(e.request, s.registers, s.bits, s.ok, line);
            text += line + "\n";
        }
        out.emplace_back(e.request.id, std::move(text));
    }
    return out;
}

// ================================================== l'enregistrement continu ===
std::string MultiPoller::recordLine(double wall, bool header) const {
    std::string line = header ? std::string("date;heure") : dateText(wall) + ";" + clockText(wall);
    for (const auto& e : entries_)
        for (std::size_t i = 0; i < e.request.values.size(); ++i) {
            const auto& spec = e.request.values[i];
            line += ';';
            if (header) {
                line += csvCell(columnTitle(e.request, spec));
                continue;
            }
            if (i < e.state.values.size() && e.state.values[i].has)
                line += csvCell(csvValue(spec, e.state.values[i].number, e.state.values[i].text));
        }
    return line;
}

bool MultiPoller::startRecording(const std::string& folder, const std::string& setName, std::string* why) {
    stopRecording();
    std::error_code ec;
    fs::create_directories(pathOf(folder), ec);
    if (ec) return fail(why, "dossier " + folder + " : " + ec.message());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        recordFolder_ = folder;
        recordSet_ = setName;
        recordError_.clear();
        recordHeader_.clear();
        recordFile_ = folder + "/" + dateText(wallNow(), true) + "_" + fileStem(setName) + ".csv";
    }
    recorderStop_ = std::make_shared<std::atomic<bool>>(false);
    recording_ = true;
    auto stop = recorderStop_;
    recorder_ = std::thread([this, stop] { recorderLoop(stop); });
    return true;
}

void MultiPoller::stopRecording() {
    if (recorderStop_) recorderStop_->store(true);
    if (recorder_.joinable()) recorder_.join();
    recorderStop_.reset();
    recording_ = false;
}

std::string MultiPoller::recordFile() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recordFile_;
}

std::string MultiPoller::recordError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recordError_;
}

void MultiPoller::recorderLoop(std::shared_ptr<std::atomic<bool>> stop) {
    std::ofstream out;
    std::string openFile;
    double next = monoNow();
    while (!stop->load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const double now = monoNow();
        if (now < next) continue;
        std::string header, line, file, folder;
        double period = 0.5;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            period = std::max(0.02, options_.periodMs / 1000.0);
            if (running_.load()) {
                const double wall = wallNow();
                header = recordLine(wall, true);
                line = recordLine(wall, false);
                folder = recordFolder_;
                file = folder + "/" + dateText(wall, true) + "_" + fileStem(recordSet_) + ".csv";
                recordFile_ = file;
            }
        }
        next += period;
        if (next < now) next = now + period;
        if (line.empty()) continue;
        // Un fichier par jour : a minuit, le suivant.
        if (file != openFile) {
            if (out.is_open()) out.close();
            std::error_code ec;
            const auto path = pathOf(file);
            const bool fresh = !fs::exists(path, ec) || fs::file_size(path, ec) == 0;
            out.open(path, std::ios::binary | std::ios::app);
            if (!out) {
                std::lock_guard<std::mutex> lock(mutex_);
                recordError_ = "\xC3\xA9" "criture impossible : " + file;
                openFile.clear();
                continue;
            }
            openFile = file;
            if (fresh) out << "\xEF\xBB\xBF";
            std::lock_guard<std::mutex> lock(mutex_);
            if (!fresh) recordHeader_ = header;                // la reprise d'un fichier du jour : ses titres sont deja la
            else recordHeader_.clear();
        }
        bool newHeader = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (header != recordHeader_) {
                recordHeader_ = header;
                newHeader = true;
            }
        }
        // Les colonnes ont change (une requete en plus) : de nouveaux titres.
        if (newHeader) out << header << "\n";
        out << line << "\n";
        out.flush();
        recorded_.fetch_add(1);
    }
    if (out.is_open()) out.close();
}

// ================================================= regrouper des variables ===
std::vector<PlannedRead> planReads(const std::vector<ReadItem>& items, bool merge, int gap, int maxWords, int maxBits) {
    std::vector<PlannedRead> out;
    for (const int fn : {3, 4, 1, 2}) {
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < items.size(); ++i)
            if (items[i].function == fn) order.push_back(i);
        if (order.empty()) continue;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (items[a].offset != items[b].offset) return items[a].offset < items[b].offset;
            return items[a].words > items[b].words;
        });
        const bool bits = bitFunction(fn);
        const int limit = bits ? std::clamp(maxBits, 1, modbus::kMaxReadBits) : std::clamp(maxWords, 1, modbus::kMaxReadRegisters);
        const long long tolerance = merge ? static_cast<long long>(std::max(0, gap)) * (bits ? 16LL : 1LL) : 0LL;
        PlannedRead cur;
        bool open = false;
        for (const auto i : order) {
            const auto& it = items[i];
            const int first = std::clamp(it.offset, 0, 65535);
            const int size = bits ? 1 : std::clamp(it.words, 1, limit);
            const int end = std::min(65536, first + size);
            if (open) {
                const int curEnd = cur.address + cur.count;
                const long long hole = static_cast<long long>(first) - curEnd;
                const int newEnd = std::max(curEnd, end);
                if (hole <= tolerance && newEnd - cur.address <= limit) {
                    cur.count = newEnd - cur.address;
                    cur.items.push_back(i);
                    continue;
                }
                out.push_back(std::move(cur));
                cur = PlannedRead{};
            }
            cur.function = fn;
            cur.address = first;
            cur.count = end - first;
            cur.items = {i};
            open = true;
        }
        if (open) out.push_back(std::move(cur));
    }
    // Les mots (bits) lus au passage : la requete moins ce que ses variables couvrent.
    for (auto& r : out) {
        std::vector<bool> covered(static_cast<std::size_t>(std::max(0, r.count)), false);
        for (const auto i : r.items) {
            const auto& it = items[i];
            const int size = bitFunction(r.function) ? 1 : std::max(1, it.words);
            for (int k = 0; k < size; ++k) {
                const int at = it.offset - r.address + k;
                if (at >= 0 && at < r.count) covered[static_cast<std::size_t>(at)] = true;
            }
        }
        r.hidden = static_cast<int>(std::count(covered.begin(), covered.end(), false));
    }
    return out;
}

std::vector<ValueSpec> PlannedRead::values(const std::vector<ReadItem>& list) const {
    std::vector<ValueSpec> out;
    for (const auto i : items) {
        if (i >= list.size()) continue;
        const auto& it = list[i];
        ValueSpec v = specForType(it.name, it.type, it.offset - address, it.bit, it.words);
        if (bitFunction(function)) {
            v.kind = ValueKind::Bit;
            v.bit = -1;
            v.words = 1;
        }
        v.scaled = it.scaled;
        v.rawMin = it.rawMin;
        v.rawMax = it.rawMax;
        v.scaleMin = it.scaleMin;
        v.scaleMax = it.scaleMax;
        v.unit = it.unit;
        out.push_back(std::move(v));
    }
    return out;
}

// ============================================================ les libelles ===
std::string functionShort(int function) {
    switch (function) {
        case 1: return "1 \xC2\xB7 bits";
        case 2: return "2 \xC2\xB7 bits";
        case 3: return "3 \xC2\xB7 mots";
        case 4: return "4 \xC2\xB7 mots";
        default: return std::to_string(function);
    }
}

std::string functionLong(int function) {
    switch (function) {
        case 1: return "1 - lire des bits (bobines)";
        case 2: return "2 - lire des bits (entr\xC3\xA9" "es)";
        case 3: return "3 - lire des mots (maintien)";
        case 4: return "4 - lire des mots (entr\xC3\xA9" "es)";
        default: return std::to_string(function);
    }
}

std::string modiconOf(int function, int address) {
    const int n = address + 1;
    char b[32];
    switch (function) {
        case 1: case 5: case 15: std::snprintf(b, sizeof b, "%05d", n); break;
        case 2: std::snprintf(b, sizeof b, "1%04d", n); break;
        case 4: std::snprintf(b, sizeof b, "3%04d", n); break;
        default: std::snprintf(b, sizeof b, n > 9999 ? "4%05d" : "4%04d", n); break;
    }
    return b;
}

std::string schneiderOf(int function, int address, int bit) {
    switch (function) {
        case 1: return "%M" + std::to_string(address);
        case 2: return "%I" + std::to_string(address);
        case 4: return "%IW" + std::to_string(address);
        default: return "%MW" + std::to_string(address) + (bit >= 0 ? "." + std::to_string(bit) : std::string{});
    }
}

std::string_view formatKey(Format f) noexcept {
    switch (f) {
        case Format::Unsigned: return "decimal";
        case Format::Signed: return "decimal_signe";
        case Format::Hex: return "hexa";
        case Format::Binary: return "binaire";
        case Format::Ascii: return "ascii";
        case Format::Unsigned32: return "32_non_signe";
        case Format::Signed32: return "32_signe";
        case Format::Float32: return "flottant";
    }
    return "decimal";
}

std::optional<Format> formatFromKey(std::string_view key) noexcept {
    for (const auto f : {Format::Unsigned, Format::Signed, Format::Hex, Format::Binary, Format::Ascii, Format::Unsigned32, Format::Signed32,
                         Format::Float32})
        if (formatKey(f) == key) return f;
    return std::nullopt;
}

std::string formatShort(Format f) {
    switch (f) {
        case Format::Unsigned: return "D\xC3\xA9" "cimal";
        case Format::Signed: return "D\xC3\xA9" "cimal sign\xC3\xA9";
        case Format::Hex: return "Hexa";
        case Format::Binary: return "Binaire";
        case Format::Ascii: return "ASCII";
        case Format::Unsigned32: return "Entier 32 ns";
        case Format::Signed32: return "Entier 32";
        case Format::Float32: return "Flottant 32";
    }
    return {};
}

} // namespace hmi::mbtool
