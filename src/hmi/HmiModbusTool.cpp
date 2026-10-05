// hmi/HmiModbusTool.cpp - l'outil Modbus (lot 15) : les requetes, les trames en
// hexa et en clair, les formats des valeurs, la lecture cyclique.
#include "HmiModbusTool.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <utility>

namespace hmi::mbtool {

namespace {

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

double wallSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::uint16_t be16(const std::vector<std::uint8_t>& b, std::size_t at) {
    return static_cast<std::uint16_t>((b[at] << 8) | b[at + 1]);
}

std::uint32_t join32(std::uint16_t first, std::uint16_t second, bool lowFirst) {
    return lowFirst ? (static_cast<std::uint32_t>(second) << 16) | first : (static_cast<std::uint32_t>(first) << 16) | second;
}

std::string clock(double t) {
    const auto secs = static_cast<std::time_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(std::fmod(t, 1.0) * 1000.0));
    return b;
}

std::string functionLabel(int f) {
    switch (f) {
        case 1: return "lire des bits (1)";
        case 2: return "lire des bits d'entr\xC3\xA9" "e (2)";
        case 3: return "lire des mots (3)";
        case 4: return "lire des mots d'entr\xC3\xA9" "e (4)";
        case 5: return "\xC3\xA9" "crire un bit (5)";
        case 6: return "\xC3\xA9" "crire un mot (6)";
        case 15: return "\xC3\xA9" "crire des bits (15)";
        case 16: return "\xC3\xA9" "crire des mots (16)";
        case 43: return "lire l'identification (43)";
        default: return "fonction " + std::to_string(f);
    }
}

} // namespace

bool validQuery(const Query& q, std::string* why) {
    if (q.address < 0 || q.address > 65535) return fail(why, "adresse hors de 0 \xC3\xA0 65535");
    switch (q.function) {
        case 1: case 2:
            if (q.count < 1 || q.count > modbus::kMaxReadBits) return fail(why, "de 1 \xC3\xA0 2000 bits par requ\xC3\xAAte");
            break;
        case 3: case 4:
            if (q.count < 1 || q.count > modbus::kMaxReadRegisters) return fail(why, "de 1 \xC3\xA0 125 mots par requ\xC3\xAAte");
            break;
        case 5:
            if (q.bits.empty()) return fail(why, "la valeur du bit \xC3\xA0 \xC3\xA9" "crire manque");
            return true;
        case 6:
            if (q.registers.empty()) return fail(why, "la valeur du mot \xC3\xA0 \xC3\xA9" "crire manque");
            return true;
        case 15:
            if (q.bits.empty() || static_cast<int>(q.bits.size()) > modbus::kMaxWriteBits) return fail(why, "de 1 \xC3\xA0 1968 bits \xC3\xA0 \xC3\xA9" "crire");
            if (q.address + static_cast<int>(q.bits.size()) > 65536) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
            return true;
        case 16:
            if (q.registers.empty() || static_cast<int>(q.registers.size()) > modbus::kMaxWriteRegisters)
                return fail(why, "de 1 \xC3\xA0 123 mots \xC3\xA0 \xC3\xA9" "crire");
            if (q.address + static_cast<int>(q.registers.size()) > 65536) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
            return true;
        case 43: return true;
        default: return fail(why, "fonction " + std::to_string(q.function) + " : 1, 2, 3, 4, 5, 6, 15, 16 ou 43");
    }
    if (q.address + q.count > 65536) return fail(why, "au-del\xC3\xA0 de l'adresse 65535");
    return true;
}

// ------------------------------------------------------------------ Session ---
void Session::setTarget(Target t) {
    if (t == target_) return;
    target_ = std::move(t);
    client_.disconnect();
}

void Session::close() { client_.disconnect(); }

bool Session::ensure(Reply& r) {
    if (client_.connected()) return true;
    modbus::Client::Settings s;
    s.host = target_.host;
    s.port = target_.port;
    s.unit = target_.unit;
    s.timeoutMs = target_.timeoutMs;
    client_.setSettings(s);
    const auto o = client_.connect();
    if (!o.ok) {
        r.why = o.why.empty() ? "connexion impossible" : o.why;
        r.ms = o.ms;
        return false;
    }
    modbus::labelClient(client_.localName(), "Outil Modbus");      // lot 17
    return true;
}

Reply Session::run(const Query& q) {
    Reply r;
    if (!validQuery(q, &r.why)) return r;
    if (!ensure(r)) return r;
    modbus::Request rq;
    rq.function = static_cast<std::uint8_t>(q.function);
    rq.address = static_cast<std::uint16_t>(q.address);
    switch (q.function) {
        case 1: case 2: case 3: case 4: rq.count = static_cast<std::uint16_t>(q.count); break;
        case 5: rq.bits = {q.bits.front()}; rq.count = 1; break;
        case 6: rq.registers = {q.registers.front()}; rq.count = 1; break;
        case 15: rq.bits = q.bits; rq.count = static_cast<std::uint16_t>(q.bits.size()); break;
        case 16: rq.registers = q.registers; rq.count = static_cast<std::uint16_t>(q.registers.size()); break;
        case 43: rq.idCode = 1; break;
        default: break;
    }
    modbus::Response rs;
    const auto o = client_.exchange(rq, rs);
    r.request = modbus::encode(rq);
    if (o.ok || o.exception) r.response = modbus::encode(rs);
    r.ok = o.ok;
    r.exception = o.exception;
    r.timeout = o.timeout;
    r.ms = o.ms;
    r.why = o.why;
    r.registers = std::move(rs.registers);
    r.bits = std::move(rs.bits);
    r.objects = std::move(rs.objects);
    if (q.function == 1 || q.function == 2)
        if (static_cast<int>(r.bits.size()) > q.count) r.bits.resize(static_cast<std::size_t>(q.count));
    if (o.lost) client_.disconnect();
    return r;
}

Reply Session::raw(const modbus::Frame& frame) {
    Reply r;
    r.request = frame;
    if (frame.size() < 8) {
        r.why = "une trame Modbus TCP fait au moins 8 octets (l'en-t\xC3\xAAte de 7, puis la fonction)";
        return r;
    }
    if (!ensure(r)) return r;
    modbus::Frame back;
    const auto o = client_.raw(frame, back);
    r.response = std::move(back);
    r.ok = o.ok;
    r.exception = o.exception;
    r.timeout = o.timeout;
    r.ms = o.ms;
    r.why = o.why;
    if (o.lost) client_.disconnect();
    return r;
}

// ------------------------------------------------------------------ trames ---
std::string hex(const std::vector<std::uint8_t>& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i) out += ' ';
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

bool parseHex(std::string_view text, std::vector<std::uint8_t>& out, std::string* why) {
    out.clear();
    std::string digits;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            ++i;
            continue;
        }
        if (std::isxdigit(static_cast<unsigned char>(c))) digits += c;
        else if (c == ' ' || c == ',' || c == ';' || c == ':' || c == '-' || c == '\t' || c == '\n' || c == '\r') continue;
        else return fail(why, std::string("caract\xC3\xA8re \xC2\xAB ") + c + " \xC2\xBB : de l'hexad\xC3\xA9" "cimal (0 \xC3\xA0 9, A \xC3\xA0 F)");
    }
    if (digits.size() % 2) return fail(why, "un nombre impair de chiffres : deux par octet");
    for (std::size_t i = 0; i < digits.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoi(digits.substr(i, 2), nullptr, 16)));
    if (out.empty()) return fail(why, "trame vide");
    return true;
}

modbus::Frame buildFrame(const Query& q, int unit, std::uint16_t transaction) {
    modbus::Request rq;
    rq.transaction = transaction;
    rq.unit = static_cast<std::uint8_t>(std::clamp(unit, 0, 255));
    rq.function = static_cast<std::uint8_t>(q.function);
    rq.address = static_cast<std::uint16_t>(std::clamp(q.address, 0, 65535));
    switch (q.function) {
        case 5: rq.bits = {q.bits.empty() ? false : q.bits.front()}; rq.count = 1; break;
        case 6: rq.registers = {q.registers.empty() ? std::uint16_t(0) : q.registers.front()}; rq.count = 1; break;
        case 15: rq.bits = q.bits; rq.count = static_cast<std::uint16_t>(q.bits.size()); break;
        case 16: rq.registers = q.registers; rq.count = static_cast<std::uint16_t>(q.registers.size()); break;
        case 43: rq.idCode = 1; break;
        default: rq.count = static_cast<std::uint16_t>(std::clamp(q.count, 0, 65535)); break;
    }
    return modbus::encode(rq);
}

std::string describe(const std::vector<std::uint8_t>& adu, bool request, int askedFunction) {
    if (adu.size() < 8) return "trame trop courte (" + std::to_string(adu.size()) + " octets)";
    const int tid = be16(adu, 0), proto = be16(adu, 2), len = be16(adu, 4), unit = adu[6], fc = adu[7];
    std::string out = "transaction " + std::to_string(tid) + ", esclave " + std::to_string(unit) + " : ";
    if (proto != 0) return out + "protocole " + std::to_string(proto) + " (pas Modbus)";
    std::string note;
    if (len != static_cast<int>(adu.size()) - 6)
        note = " [longueur annonc\xC3\xA9" "e " + std::to_string(len) + ", re\xC3\xA7u" "e " + std::to_string(adu.size() - 6) + "]";
    if (fc & 0x80) {
        const int e = adu.size() > 8 ? adu[8] : 0;
        return out + "exception " + modbus::exceptionText(e) + " \xC3\xA0 " + functionLabel(fc & 0x7F) + note;
    }
    const auto word = [&](std::size_t at) -> int { return at + 1 < adu.size() ? be16(adu, at) : -1; };
    if (request) {
        switch (fc) {
            case 1: case 2: return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + ", " + std::to_string(word(10)) + " bits" + note;
            case 3: case 4: return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + ", " + std::to_string(word(10)) + " mots" + note;
            case 5: return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + " = " + (word(10) == 0xFF00 ? "1" : "0") + note;
            case 6: return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + " = " + std::to_string(word(10)) + note;
            case 15: return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + ", " + std::to_string(word(10)) + " bits" + note;
            case 16: {
                std::string values;
                const int n = std::max(0, word(10));
                for (int k = 0; k < n && k < 8; ++k) values += (k ? " " : "") + std::to_string(word(13 + static_cast<std::size_t>(k) * 2));
                if (n > 8) values += " ...";
                return out + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + ", " + std::to_string(n) + " mots : " + values + note;
            }
            case 43: return out + functionLabel(fc) + note;
            default: return out + functionLabel(fc) + note;
        }
    }
    const int f = askedFunction ? askedFunction : fc;
    switch (f) {
        case 1: case 2: {
            const int bytes = adu[8];
            std::string bits;
            for (int k = 0; k < bytes && k < 2; ++k)
                for (int b = 0; b < 8; ++b)
                    if (9 + static_cast<std::size_t>(k) < adu.size()) bits += ((adu[9 + static_cast<std::size_t>(k)] >> b) & 1) ? '1' : '0';
            return out + "r\xC3\xA9ponse " + functionLabel(fc) + ", " + std::to_string(bytes) + " octet(s) : " + bits + (bytes > 2 ? " ..." : "") + note;
        }
        case 3: case 4: {
            const int bytes = adu[8];
            std::string values;
            const int n = bytes / 2;
            for (int k = 0; k < n && k < 10; ++k) values += (k ? " " : "") + std::to_string(word(9 + static_cast<std::size_t>(k) * 2));
            if (n > 10) values += " ...";
            return out + "r\xC3\xA9ponse " + functionLabel(fc) + ", " + std::to_string(n) + " mots : " + values + note;
        }
        case 5: case 6:
            return out + "r\xC3\xA9ponse " + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + " = " + std::to_string(word(10)) + note;
        case 15: case 16:
            return out + "r\xC3\xA9ponse " + functionLabel(fc) + ", adresse " + std::to_string(word(8)) + ", " + std::to_string(word(10)) + " \xC3\xA9" "crits" + note;
        case 43: return out + "r\xC3\xA9ponse " + functionLabel(fc) + note;
        default: return out + "r\xC3\xA9ponse " + functionLabel(fc) + note;
    }
}

// ------------------------------------------------------------------ valeurs ---
const std::vector<std::string>& formatLabels() {
    static const std::vector<std::string> labels{
        "D\xC3\xA9" "cimal non sign\xC3\xA9", "D\xC3\xA9" "cimal sign\xC3\xA9", "Hexad\xC3\xA9" "cimal", "Binaire", "ASCII",
        "32 bits non sign\xC3\xA9", "32 bits sign\xC3\xA9", "Flottant 32 bits"};
    return labels;
}

Format formatFrom(std::string_view label) noexcept {
    const auto& l = formatLabels();
    for (std::size_t i = 0; i < l.size(); ++i)
        if (l[i] == label) return static_cast<Format>(i);
    return Format::Unsigned;
}

bool wide(Format f) noexcept { return f == Format::Unsigned32 || f == Format::Signed32 || f == Format::Float32; }

std::string formatValue(const std::vector<std::uint16_t>& regs, std::size_t i, Format f, bool lowFirst) {
    if (i >= regs.size()) return {};
    const std::uint16_t w = regs[i];
    char b[64];
    switch (f) {
        case Format::Unsigned: return std::to_string(w);
        case Format::Signed: return std::to_string(static_cast<std::int16_t>(w));
        case Format::Hex: std::snprintf(b, sizeof b, "0x%04X", w); return b;
        case Format::Binary: {
            std::string s;
            for (int k = 15; k >= 0; --k) {
                s += ((w >> k) & 1) ? '1' : '0';
                if (k % 4 == 0 && k) s += ' ';
            }
            return s;
        }
        case Format::Ascii: {
            std::string s;
            for (const int c : {w >> 8, w & 0xFF}) s += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
            return s;
        }
        default: break;
    }
    if (i + 1 >= regs.size()) return "\xE2\x80\x94";
    const std::uint32_t v = join32(regs[i], regs[i + 1], lowFirst);
    switch (f) {
        case Format::Unsigned32: return std::to_string(v);
        case Format::Signed32: return std::to_string(static_cast<std::int32_t>(v));
        case Format::Float32: {
            float x = 0;
            std::memcpy(&x, &v, sizeof x);
            std::snprintf(b, sizeof b, "%.7g", static_cast<double>(x));
            return b;
        }
        default: return {};
    }
}

double numericValue(const std::vector<std::uint16_t>& regs, std::size_t i, Format f, bool lowFirst) {
    if (i >= regs.size()) return std::numeric_limits<double>::quiet_NaN();
    const std::uint16_t w = regs[i];
    switch (f) {
        case Format::Signed: return static_cast<std::int16_t>(w);
        case Format::Unsigned32: case Format::Signed32: case Format::Float32: {
            if (i + 1 >= regs.size()) return std::numeric_limits<double>::quiet_NaN();
            const std::uint32_t v = join32(regs[i], regs[i + 1], lowFirst);
            if (f == Format::Unsigned32) return v;
            if (f == Format::Signed32) return static_cast<std::int32_t>(v);
            float x = 0;
            std::memcpy(&x, &v, sizeof x);
            return x;
        }
        default: return w;
    }
}

bool parseValue(std::string_view text, Format f, bool lowFirst, std::vector<std::uint16_t>& out, std::string* why) {
    out.clear();
    std::string t(text);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
    if (t.empty()) return fail(why, "valeur vide");
    const auto two = [&](std::uint32_t v) {
        const auto hi = static_cast<std::uint16_t>(v >> 16), lo = static_cast<std::uint16_t>(v & 0xFFFF);
        out = lowFirst ? std::vector<std::uint16_t>{lo, hi} : std::vector<std::uint16_t>{hi, lo};
        return true;
    };
    try {
        switch (f) {
            case Format::Unsigned: {
                const long long v = std::stoll(t);
                if (v < 0 || v > 65535) return fail(why, "de 0 \xC3\xA0 65535");
                out = {static_cast<std::uint16_t>(v)};
                return true;
            }
            case Format::Signed: {
                const long long v = std::stoll(t);
                if (v < -32768 || v > 32767) return fail(why, "de -32768 \xC3\xA0 32767");
                out = {static_cast<std::uint16_t>(static_cast<std::int16_t>(v))};
                return true;
            }
            case Format::Hex: {
                std::string h = t;
                if (h.size() > 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h = h.substr(2);
                std::size_t used = 0;
                const unsigned long v = std::stoul(h, &used, 16);
                if (used != h.size() || v > 0xFFFF) return fail(why, "de 0x0000 \xC3\xA0 0xFFFF");
                out = {static_cast<std::uint16_t>(v)};
                return true;
            }
            case Format::Binary: {
                std::uint32_t v = 0;
                int n = 0;
                for (const char c : t) {
                    if (c == ' ' || c == '_') continue;
                    if (c != '0' && c != '1') return fail(why, "des 0 et des 1");
                    v = (v << 1) | static_cast<std::uint32_t>(c - '0');
                    if (++n > 16) return fail(why, "16 bits au plus");
                }
                out = {static_cast<std::uint16_t>(v)};
                return true;
            }
            case Format::Ascii: {
                if (t.size() > 2) return fail(why, "deux caract\xC3\xA8res par mot");
                const auto hi = static_cast<unsigned char>(t[0]);
                const auto lo = t.size() > 1 ? static_cast<unsigned char>(t[1]) : 0;
                out = {static_cast<std::uint16_t>((hi << 8) | lo)};
                return true;
            }
            case Format::Unsigned32: {
                const long long v = std::stoll(t);
                if (v < 0 || v > 4294967295LL) return fail(why, "de 0 \xC3\xA0 4294967295");
                return two(static_cast<std::uint32_t>(v));
            }
            case Format::Signed32: {
                const long long v = std::stoll(t);
                if (v < -2147483648LL || v > 2147483647LL) return fail(why, "de -2147483648 \xC3\xA0 2147483647");
                return two(static_cast<std::uint32_t>(static_cast<std::int32_t>(v)));
            }
            case Format::Float32: {
                std::string n = t;
                std::replace(n.begin(), n.end(), ',', '.');
                const float x = std::stof(n);
                std::uint32_t v = 0;
                std::memcpy(&v, &x, sizeof v);
                return two(v);
            }
        }
    } catch (...) {
        return fail(why, "nombre illisible \xC2\xAB " + t + " \xC2\xBB");
    }
    return false;
}

// ------------------------------------------------------------ lecture cyclique ---
Poller::~Poller() { stop(); }

void Poller::start(Target t, Query q, int periodMs) {
    stop();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_ = std::move(t);
        query_ = std::move(q);
        periodMs_ = std::max(50, periodMs);
        samples_.clear();
        stats_ = Stats{};
        sumMs_ = 0;
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void Poller::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

void Poller::loop() {
    Target t;
    Query q;
    int period = 1000;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        t = target_;
        q = query_;
        period = periodMs_;
    }
    Session session(t);
    auto next = std::chrono::steady_clock::now();
    while (!stop_) {
        const Reply r = session.run(q);
        Sample s;
        s.t = wallSeconds();
        s.ok = r.ok;
        s.ms = r.ms;
        s.registers = r.registers;
        s.bits = r.bits;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++stats_.requests;
            if (!r.ok) {
                ++stats_.errors;
                stats_.lastError = r.why;
            } else {
                stats_.lastMs = r.ms;
                sumMs_ += r.ms;
                stats_.avgMs = sumMs_ / static_cast<double>(stats_.requests - stats_.errors);
                stats_.maxMs = std::max(stats_.maxMs, r.ms);
            }
            samples_.push_back(std::move(s));
            // Un quart d'heure, et 20 000 echantillons au plus.
            while (!samples_.empty() && (samples_.size() > 20000 || samples_.front().t < samples_.back().t - 900.0)) samples_.pop_front();
        }
        next += std::chrono::milliseconds(period);
        // En retard (un delai depasse) : on repart de maintenant.
        if (next < std::chrono::steady_clock::now()) next = std::chrono::steady_clock::now();
        while (!stop_ && std::chrono::steady_clock::now() < next) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    session.close();
}

Poller::Stats Poller::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

Poller::Sample Poller::last() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return samples_.empty() ? Sample{} : samples_.back();
}

std::vector<Poller::Sample> Poller::history(double seconds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Sample> out;
    if (samples_.empty()) return out;
    const double from = samples_.back().t - seconds;
    for (const auto& s : samples_)
        if (s.t >= from) out.push_back(s);
    return out;
}

void Poller::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    stats_ = Stats{};
    sumMs_ = 0;
}

Query Poller::query() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return query_;
}

Target Poller::target() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return target_;
}

std::string Poller::csv(Format f, bool lowFirst) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out = "heure;ms;etat";
    const bool bits = query_.function == 1 || query_.function == 2;
    const int step = !bits && wide(f) ? 2 : 1;
    for (int k = 0; k < query_.count; k += step) out += ";" + std::to_string(query_.address + k);
    out += "\n";
    for (const auto& s : samples_) {
        char ms[32];
        std::snprintf(ms, sizeof ms, "%.1f", s.ms);
        out += clock(s.t) + ";" + ms + ";" + (s.ok ? "ok" : "erreur");
        for (int k = 0; k < query_.count; k += step) {
            out += ";";
            if (!s.ok) continue;
            if (bits) {
                if (static_cast<std::size_t>(k) < s.bits.size()) out += s.bits[static_cast<std::size_t>(k)] ? "1" : "0";
            } else {
                std::string v = formatValue(s.registers, static_cast<std::size_t>(k), f, lowFirst);
                std::replace(v.begin(), v.end(), ';', ',');
                out += v;
            }
        }
        out += "\n";
    }
    return out;
}

} // namespace hmi::mbtool
