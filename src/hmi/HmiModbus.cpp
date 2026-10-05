// hmi/HmiModbus.cpp - Modbus TCP : trames, client, serveur (lot 14).
#include "HmiModbus.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hmi::modbus {

namespace {

using Clock = std::chrono::steady_clock;

void put16(Frame& f, std::uint16_t v) {
    f.push_back(static_cast<std::uint8_t>(v >> 8));
    f.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint16_t get16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

void packBits(Frame& f, const std::vector<bool>& bits) {
    const std::size_t bytes = (bits.size() + 7) / 8;
    f.push_back(static_cast<std::uint8_t>(bytes));
    for (std::size_t b = 0; b < bytes; ++b) {
        std::uint8_t v = 0;
        for (std::size_t k = 0; k < 8 && b * 8 + k < bits.size(); ++k)
            if (bits[b * 8 + k]) v = static_cast<std::uint8_t>(v | (1u << k));
        f.push_back(v);
    }
}

std::vector<bool> unpackBits(const std::uint8_t* p, std::size_t count) {
    std::vector<bool> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = (p[i / 8] >> (i % 8)) & 1;
    return out;
}

// L'en-tete MBAP : la longueur compte l'esclave et la PDU.
Frame header(std::uint16_t transaction, std::uint8_t unit) {
    Frame f;
    f.reserve(260);
    put16(f, transaction);
    put16(f, 0);
    put16(f, 0);   // la longueur, posee a la fin
    f.push_back(unit);
    return f;
}

void finish(Frame& f) {
    const auto len = static_cast<std::uint16_t>(f.size() - 6);
    f[4] = static_cast<std::uint8_t>(len >> 8);
    f[5] = static_cast<std::uint8_t>(len & 0xFF);
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

} // namespace

std::string exceptionText(int code) {
    switch (code) {
        case 1:  return "1 : fonction non prise en charge";
        case 2:  return "2 : adresse ill\xC3\xA9gale";
        case 3:  return "3 : valeur ill\xC3\xA9gale";
        case 4:  return "4 : d\xC3\xA9" "faut de l'\xC3\xA9quipement";
        case 5:  return "5 : requ\xC3\xAAte accept\xC3\xA9" "e, traitement long";
        case 6:  return "6 : \xC3\xA9quipement occup\xC3\xA9";
        case 8:  return "8 : erreur de parit\xC3\xA9 m\xC3\xA9moire";
        case 10: return "10 : passerelle, chemin indisponible";
        case 11: return "11 : passerelle, l'\xC3\xA9quipement ne r\xC3\xA9pond pas";
        default: return std::to_string(code) + " : exception inconnue";
    }
}

std::string functionName(int function) {
    switch (function) {
        case kReadCoils:                return "fonction 1 (lire des bits)";
        case kReadDiscreteInputs:       return "fonction 2 (lire des bits d'entr\xC3\xA9" "e)";
        case kReadHoldingRegisters:     return "fonction 3 (lire des mots)";
        case kReadInputRegisters:       return "fonction 4 (lire des mots d'entr\xC3\xA9" "e)";
        case kWriteSingleCoil:          return "fonction 5 (\xC3\xA9" "crire un bit)";
        case kWriteSingleRegister:      return "fonction 6 (\xC3\xA9" "crire un mot)";
        case kWriteMultipleCoils:       return "fonction 15 (\xC3\xA9" "crire des bits)";
        case kWriteMultipleRegisters:   return "fonction 16 (\xC3\xA9" "crire des mots)";
        case kReadDeviceIdentification: return "fonction 43 (identification)";
        default:                        return "fonction " + std::to_string(function);
    }
}

// ------------------------------------------------------------------ trames ---
Frame encode(const Request& rq) {
    Frame f = header(rq.transaction, rq.unit);
    f.push_back(rq.function);
    switch (rq.function) {
        case kReadCoils: case kReadDiscreteInputs: case kReadHoldingRegisters: case kReadInputRegisters:
            put16(f, rq.address);
            put16(f, rq.count);
            break;
        case kWriteSingleCoil:
            put16(f, rq.address);
            put16(f, !rq.bits.empty() && rq.bits.front() ? 0xFF00 : 0x0000);
            break;
        case kWriteSingleRegister:
            put16(f, rq.address);
            put16(f, rq.registers.empty() ? 0 : rq.registers.front());
            break;
        case kWriteMultipleCoils:
            put16(f, rq.address);
            put16(f, static_cast<std::uint16_t>(rq.bits.size()));
            packBits(f, rq.bits);
            break;
        case kWriteMultipleRegisters:
            put16(f, rq.address);
            put16(f, static_cast<std::uint16_t>(rq.registers.size()));
            f.push_back(static_cast<std::uint8_t>(rq.registers.size() * 2));
            for (const auto r : rq.registers) put16(f, r);
            break;
        case kReadDeviceIdentification:
            f.push_back(0x0E);          // MEI : lire l'identification
            f.push_back(rq.idCode);     // 1 de base, 2 courante
            f.push_back(0x00);          // a partir de l'objet 0
            break;
        default:
            break;
    }
    finish(f);
    return f;
}

Frame encode(const Response& rs) {
    Frame f = header(rs.transaction, rs.unit);
    if (rs.exception) {
        f.push_back(static_cast<std::uint8_t>(rs.function | 0x80));
        f.push_back(rs.exception);
        finish(f);
        return f;
    }
    f.push_back(rs.function);
    switch (rs.function) {
        case kReadCoils: case kReadDiscreteInputs:
            packBits(f, rs.bits);
            break;
        case kReadHoldingRegisters: case kReadInputRegisters:
            f.push_back(static_cast<std::uint8_t>(rs.registers.size() * 2));
            for (const auto r : rs.registers) put16(f, r);
            break;
        case kWriteSingleCoil: case kWriteSingleRegister: case kWriteMultipleCoils: case kWriteMultipleRegisters:
            put16(f, rs.address);
            put16(f, rs.value);
            break;
        case kReadDeviceIdentification: {
            f.push_back(0x0E);
            f.push_back(0x02);
            f.push_back(0x02);   // conformite : identification courante, par flux
            f.push_back(0x00);   // pas de suite
            f.push_back(0x00);
            f.push_back(static_cast<std::uint8_t>(rs.objects.size()));
            for (const auto& [id, text] : rs.objects) {
                const auto n = std::min<std::size_t>(text.size(), 200);
                f.push_back(id);
                f.push_back(static_cast<std::uint8_t>(n));
                f.insert(f.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(n));
            }
            break;
        }
        default:
            break;
    }
    finish(f);
    return f;
}

bool decodeRequest(const std::uint8_t* p, std::size_t size, Request& out, int& exception, std::string* why) {
    exception = 0;
    if (size < 8) return fail(why, "trame trop courte");
    if (get16(p + 2) != 0) return fail(why, "protocole " + std::to_string(get16(p + 2)) + " (0 attendu)");
    const std::size_t len = get16(p + 4);
    if (len < 2 || len + 6 != size) return fail(why, "longueur incoh\xC3\xA9rente");
    out = Request{};
    out.transaction = get16(p);
    out.unit = p[6];
    out.function = p[7];
    const std::uint8_t* d = p + 8;
    const std::size_t n = size - 8;
    switch (out.function) {
        case kReadCoils: case kReadDiscreteInputs: case kReadHoldingRegisters: case kReadInputRegisters: {
            if (n != 4) return fail(why, "requ\xC3\xAAte de lecture mal form\xC3\xA9" "e");
            out.address = get16(d);
            out.count = get16(d + 2);
            const bool bits = out.function <= kReadDiscreteInputs;
            const int limit = bits ? kMaxReadBits : kMaxReadRegisters;
            if (out.count < 1 || out.count > limit) exception = 3;
            else if (static_cast<std::size_t>(out.address) + out.count > 65536) exception = 2;
            return true;
        }
        case kWriteSingleCoil: {
            if (n != 4) return fail(why, "requ\xC3\xAAte mal form\xC3\xA9" "e");
            out.address = get16(d);
            const auto v = get16(d + 2);
            if (v != 0xFF00 && v != 0x0000) exception = 3;
            out.bits = {v == 0xFF00};
            out.count = 1;
            return true;
        }
        case kWriteSingleRegister:
            if (n != 4) return fail(why, "requ\xC3\xAAte mal form\xC3\xA9" "e");
            out.address = get16(d);
            out.registers = {get16(d + 2)};
            out.count = 1;
            return true;
        case kWriteMultipleCoils: {
            if (n < 5) return fail(why, "requ\xC3\xAAte mal form\xC3\xA9" "e");
            out.address = get16(d);
            out.count = get16(d + 2);
            const std::size_t bytes = d[4];
            if (bytes + 5 != n) return fail(why, "nombre d'octets incoh\xC3\xA9rent");
            if (out.count < 1 || out.count > kMaxWriteBits || bytes != (out.count + 7u) / 8u) {
                exception = 3;
                return true;
            }
            if (static_cast<std::size_t>(out.address) + out.count > 65536) exception = 2;
            out.bits = unpackBits(d + 5, out.count);
            return true;
        }
        case kWriteMultipleRegisters: {
            if (n < 5) return fail(why, "requ\xC3\xAAte mal form\xC3\xA9" "e");
            out.address = get16(d);
            out.count = get16(d + 2);
            const std::size_t bytes = d[4];
            if (bytes + 5 != n) return fail(why, "nombre d'octets incoh\xC3\xA9rent");
            if (out.count < 1 || out.count > kMaxWriteRegisters || bytes != out.count * 2u) {
                exception = 3;
                return true;
            }
            if (static_cast<std::size_t>(out.address) + out.count > 65536) exception = 2;
            for (std::size_t i = 0; i < out.count; ++i) out.registers.push_back(get16(d + 5 + 2 * i));
            return true;
        }
        case kReadDeviceIdentification:
            if (n != 3 || d[0] != 0x0E) {
                exception = 1;
                return true;
            }
            out.idCode = d[1];
            if (out.idCode < 1 || out.idCode > 4) exception = 3;
            return true;
        default:
            exception = 1;
            return true;
    }
}

bool decodeResponse(const std::uint8_t* p, std::size_t size, const Request& asked, Response& out, std::string* why) {
    if (size < 9) return fail(why, "r\xC3\xA9ponse trop courte");
    const std::size_t len = get16(p + 4);
    if (get16(p + 2) != 0 || len + 6 != size) return fail(why, "en-t\xC3\xAAte de r\xC3\xA9ponse incoh\xC3\xA9rent");
    out = Response{};
    out.transaction = get16(p);
    out.unit = p[6];
    const std::uint8_t fc = p[7];
    if (out.transaction != asked.transaction) return fail(why, "r\xC3\xA9ponse \xC3\xA0 une autre requ\xC3\xAAte");
    if ((fc & 0x7F) != asked.function) return fail(why, "r\xC3\xA9ponse \xC3\xA0 une autre fonction (" + std::to_string(fc & 0x7F) + ")");
    out.function = asked.function;
    const std::uint8_t* d = p + 8;
    const std::size_t n = size - 8;
    if (fc & 0x80) {
        if (n != 1) return fail(why, "exception mal form\xC3\xA9" "e");
        out.exception = d[0];
        return true;
    }
    switch (asked.function) {
        case kReadCoils: case kReadDiscreteInputs: {
            if (n < 1 || d[0] + 1u != n || d[0] != (asked.count + 7u) / 8u) return fail(why, "nombre d'octets inattendu");
            out.bits = unpackBits(d + 1, asked.count);
            return true;
        }
        case kReadHoldingRegisters: case kReadInputRegisters: {
            if (n < 1 || d[0] + 1u != n || d[0] != asked.count * 2u) return fail(why, "nombre d'octets inattendu");
            for (std::size_t i = 0; i < asked.count; ++i) out.registers.push_back(get16(d + 1 + 2 * i));
            return true;
        }
        case kWriteSingleCoil: case kWriteSingleRegister: case kWriteMultipleCoils: case kWriteMultipleRegisters:
            if (n != 4) return fail(why, "\xC3\xA9" "cho d'\xC3\xA9" "criture mal form\xC3\xA9");
            out.address = get16(d);
            out.value = get16(d + 2);
            if (out.address != asked.address) return fail(why, "\xC3\xA9" "cho d'une autre adresse");
            return true;
        case kReadDeviceIdentification: {
            if (n < 6 || d[0] != 0x0E) return fail(why, "identification mal form\xC3\xA9" "e");
            std::size_t at = 6;
            const std::size_t objects = d[5];
            for (std::size_t k = 0; k < objects; ++k) {
                if (at + 2 > n) return fail(why, "identification tronqu\xC3\xA9" "e");
                const std::uint8_t id = d[at];
                const std::size_t l = d[at + 1];
                if (at + 2 + l > n) return fail(why, "identification tronqu\xC3\xA9" "e");
                out.objects.emplace_back(id, std::string(reinterpret_cast<const char*>(d + at + 2), l));
                at += 2 + l;
            }
            return true;
        }
        default:
            return fail(why, "fonction non prise en charge par ce client");
    }
}

// ------------------------------------------------------------------ client ---
Outcome Client::connect() {
    Outcome o;
    const auto t0 = Clock::now();
    o.ok = socket_.connect(settings_.host, settings_.port, settings_.timeoutMs, &o.why);
    o.ms = msSince(t0);
    if (!o.ok) {
        o.lost = true;
        o.timeout = o.why.find("pas de r\xC3\xA9ponse") != std::string::npos;
    }
    return o;
}

// ----------------------------------------------------------- lot 15 : l'espion ---
namespace {
std::mutex& tapMutex() {
    static std::mutex m;
    return m;
}
Tap& tapSlot() {
    static Tap t;
    return t;
}
std::atomic<bool>& tapOn() {
    static std::atomic<bool> on{false};
    return on;
}
void tapFrame(bool sent, const net::Socket& s, const std::string& peer, const Frame& f) {
    if (!tapOn().load(std::memory_order_relaxed)) return;
    Tap copy;
    {
        std::lock_guard<std::mutex> lock(tapMutex());
        copy = tapSlot();
    }
    if (copy) copy(sent, s.localName(), peer, f);
}
} // namespace

void setTap(Tap tap) {
    std::lock_guard<std::mutex> lock(tapMutex());
    tapOn().store(static_cast<bool>(tap));
    tapSlot() = std::move(tap);
}

bool tapped() noexcept { return tapOn().load(); }

// ------------------------------------------------ lot 17 : qui parle au serveur ---
namespace {
thread_local std::string tServing;
std::mutex& labelMutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::string>& labels() {
    static std::map<std::string, std::string> l;
    return l;
}
} // namespace

std::string servingClient() { return tServing; }

void labelClient(const std::string& localName, std::string label) {
    if (localName.empty()) return;
    std::lock_guard<std::mutex> lock(labelMutex());
    auto& l = labels();
    if (l.size() > 4096) l.clear();
    l[localName] = std::move(label);
}

std::string clientLabel(const std::string& peer) {
    std::lock_guard<std::mutex> lock(labelMutex());
    const auto it = labels().find(peer);
    return it != labels().end() ? it->second : peer;
}

void Server::setDelay(int ms, int jitterMs) noexcept {
    delayMs_.store(std::max(0, ms));
    jitterMs_.store(std::max(0, jitterMs));
}

Outcome Client::exchange(Request& rq, Response& rs) {
    Outcome o;
    const auto t0 = Clock::now();
    if (!socket_.valid()) {
        o.lost = true;
        o.why = "pas de connexion";
        return o;
    }
    rq.transaction = next_++;
    if (next_ == 0) next_ = 1;
    rq.unit = static_cast<std::uint8_t>(std::clamp(settings_.unit, 0, 255));
    const Frame f = encode(rq);
    const int timeout = std::max(1, settings_.timeoutMs);
    const std::string peer = settings_.host + ":" + std::to_string(settings_.port);
    if (!socket_.sendAll(f.data(), f.size(), timeout, &o.why)) {
        o.lost = true;
        socket_.close();
        o.ms = msSince(t0);
        return o;
    }
    tapFrame(true, socket_, peer, f);                  // lot 15 : l'espion
    const auto deadline = t0 + std::chrono::milliseconds(timeout);
    for (;;) {
        const auto left = static_cast<int>(std::max<long long>(
            0, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
        std::uint8_t head[7];
        if (!socket_.receiveExact(head, sizeof head, left, &o.why)) {
            o.ms = msSince(t0);
            if (socket_.timedOut()) {
                o.timeout = true;
                o.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeout) + " ms";
            } else {
                o.lost = true;
                socket_.close();
            }
            return o;
        }
        const std::size_t len = static_cast<std::size_t>((head[4] << 8) | head[5]);
        if (len < 2 || len > 254) {
            o.lost = true;
            o.why = "trame illisible (longueur " + std::to_string(len) + ")";
            socket_.close();
            o.ms = msSince(t0);
            return o;
        }
        Frame frame(head, head + 7);
        frame.resize(6 + len);
        const auto rest = static_cast<int>(std::max<long long>(
            50, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count()));
        if (!socket_.receiveExact(frame.data() + 7, len - 1, rest, &o.why)) {
            // Une trame commencee et pas finie : le flux n'est plus aligne.
            o.lost = true;
            socket_.close();
            o.ms = msSince(t0);
            return o;
        }
        tapFrame(false, socket_, peer, frame);          // lot 15 : l'espion
        const auto tid = static_cast<std::uint16_t>((frame[0] << 8) | frame[1]);
        if (tid != rq.transaction) continue;   // la reponse tardive d'une requete abandonnee
        std::string why;
        if (!decodeResponse(frame.data(), frame.size(), rq, rs, &why)) {
            o.lost = true;
            o.why = why;
            socket_.close();
            o.ms = msSince(t0);
            return o;
        }
        o.ms = msSince(t0);
        if (rs.exception) {
            o.exception = rs.exception;
            o.why = "exception " + exceptionText(rs.exception);
            return o;
        }
        o.ok = true;
        return o;
    }
}

Outcome Client::raw(const Frame& request, Frame& response) {
    Outcome o;
    const auto t0 = Clock::now();
    response.clear();
    if (!socket_.valid()) {
        o.lost = true;
        o.why = "pas de connexion";
        return o;
    }
    const int timeout = std::max(1, settings_.timeoutMs);
    const std::string peer = settings_.host + ":" + std::to_string(settings_.port);
    if (!socket_.sendAll(request.data(), request.size(), timeout, &o.why)) {
        o.lost = true;
        socket_.close();
        o.ms = msSince(t0);
        return o;
    }
    tapFrame(true, socket_, peer, request);
    std::uint8_t head[7];
    if (!socket_.receiveExact(head, sizeof head, timeout, &o.why)) {
        o.ms = msSince(t0);
        if (socket_.timedOut()) {
            o.timeout = true;
            o.why = "pas de r\xC3\xA9ponse en " + std::to_string(timeout) + " ms";
        } else {
            o.lost = true;
            socket_.close();
        }
        return o;
    }
    const std::size_t len = static_cast<std::size_t>((head[4] << 8) | head[5]);
    response.assign(head, head + 7);
    if (len < 1 || len > 254) {
        // L'equipement repond une longueur impossible : la trame est rendue telle quelle.
        o.lost = true;
        o.why = "r\xC3\xA9ponse illisible (longueur " + std::to_string(len) + ")";
        socket_.close();
        o.ms = msSince(t0);
        return o;
    }
    response.resize(6 + len);
    if (len > 1 && !socket_.receiveExact(response.data() + 7, len - 1, std::max(50, timeout), &o.why)) {
        o.lost = true;
        socket_.close();
        o.ms = msSince(t0);
        return o;
    }
    tapFrame(false, socket_, peer, response);
    o.ms = msSince(t0);
    if (response.size() > 8 && (response[7] & 0x80)) {
        o.exception = response[8];
        o.why = "exception " + exceptionText(response[8]);
        return o;
    }
    o.ok = true;
    return o;
}

Outcome Client::readBits(bool discrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) {
    Request rq;
    rq.function = discrete ? kReadDiscreteInputs : kReadCoils;
    rq.address = address;
    rq.count = count;
    Response rs;
    auto o = exchange(rq, rs);
    if (o.ok) out = std::move(rs.bits);
    return o;
}

Outcome Client::readRegisters(bool input, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) {
    Request rq;
    rq.function = input ? kReadInputRegisters : kReadHoldingRegisters;
    rq.address = address;
    rq.count = count;
    Response rs;
    auto o = exchange(rq, rs);
    if (o.ok) out = std::move(rs.registers);
    return o;
}

Outcome Client::writeCoil(std::uint16_t address, bool value) {
    Request rq;
    rq.function = kWriteSingleCoil;
    rq.address = address;
    rq.bits = {value};
    Response rs;
    return exchange(rq, rs);
}

Outcome Client::writeRegister(std::uint16_t address, std::uint16_t value) {
    Request rq;
    rq.function = kWriteSingleRegister;
    rq.address = address;
    rq.registers = {value};
    Response rs;
    return exchange(rq, rs);
}

Outcome Client::writeCoils(std::uint16_t address, const std::vector<bool>& values) {
    if (values.size() == 1) return writeCoil(address, values.front());
    Request rq;
    rq.function = kWriteMultipleCoils;
    rq.address = address;
    rq.bits = values;
    Response rs;
    return exchange(rq, rs);
}

Outcome Client::writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) {
    if (values.size() == 1) return writeRegister(address, values.front());
    Request rq;
    rq.function = kWriteMultipleRegisters;
    rq.address = address;
    rq.registers = values;
    Response rs;
    return exchange(rq, rs);
}

Outcome Client::readIdentification(Identification& out) {
    Request rq;
    rq.function = kReadDeviceIdentification;
    rq.idCode = 2;
    Response rs;
    auto o = exchange(rq, rs);
    if (!o.ok && o.exception == 3) {   // l'identification courante n'est pas servie : celle de base
        rq.idCode = 1;
        rs = Response{};
        o = exchange(rq, rs);
    }
    if (o.ok) out = std::move(rs.objects);
    return o;
}

// ----------------------------------------------------------------- memoire ---
MemoryBank::MemoryBank(std::size_t bits, std::size_t words)
    : words_(std::min<std::size_t>(words, 65536), 0), bits_(std::min<std::size_t>(bits, 65536), 0) {}

int MemoryBank::readBits(bool, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + count > bits_.size()) return 2;
    out.assign(count, false);
    for (std::size_t i = 0; i < count; ++i) out[i] = bits_[address + i] != 0;
    return 0;
}

int MemoryBank::readRegisters(bool, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + count > words_.size()) return 2;
    out.assign(words_.begin() + address, words_.begin() + address + count);
    return 0;
}

int MemoryBank::writeBits(std::uint16_t address, const std::vector<bool>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + values.size() > bits_.size()) return 2;
    for (std::size_t i = 0; i < values.size(); ++i) bits_[address + i] = values[i] ? 1 : 0;
    return 0;
}

int MemoryBank::writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<std::size_t>(address) + values.size() > words_.size()) return 2;
    std::copy(values.begin(), values.end(), words_.begin() + address);
    return 0;
}

Identification MemoryBank::identification() {
    std::lock_guard<std::mutex> lock(mutex_);
    return id_;
}

void MemoryBank::setIdentification(Identification id) {
    std::lock_guard<std::mutex> lock(mutex_);
    id_ = std::move(id);
}

std::uint16_t MemoryBank::word(std::size_t address) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return address < words_.size() ? words_[address] : 0;
}

void MemoryBank::setWord(std::size_t address, std::uint16_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (address < words_.size()) words_[address] = value;
}

bool MemoryBank::bit(std::size_t address) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return address < bits_.size() && bits_[address] != 0;
}

void MemoryBank::setBit(std::size_t address, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (address < bits_.size()) bits_[address] = value ? 1 : 0;
}

Response serve(Bank& bank, const Request& rq) {
    Response rs;
    rs.transaction = rq.transaction;
    rs.unit = rq.unit;
    rs.function = rq.function;
    int ex = 0;
    switch (rq.function) {
        case kReadCoils: case kReadDiscreteInputs:
            ex = bank.readBits(rq.function == kReadDiscreteInputs, rq.address, rq.count, rs.bits);
            break;
        case kReadHoldingRegisters: case kReadInputRegisters:
            ex = bank.readRegisters(rq.function == kReadInputRegisters, rq.address, rq.count, rs.registers);
            break;
        case kWriteSingleCoil:
            ex = bank.writeBits(rq.address, rq.bits);
            rs.address = rq.address;
            rs.value = !rq.bits.empty() && rq.bits.front() ? 0xFF00 : 0x0000;
            break;
        case kWriteSingleRegister:
            ex = bank.writeRegisters(rq.address, rq.registers);
            rs.address = rq.address;
            rs.value = rq.registers.empty() ? 0 : rq.registers.front();
            break;
        case kWriteMultipleCoils:
            ex = bank.writeBits(rq.address, rq.bits);
            rs.address = rq.address;
            rs.value = static_cast<std::uint16_t>(rq.bits.size());
            break;
        case kWriteMultipleRegisters:
            ex = bank.writeRegisters(rq.address, rq.registers);
            rs.address = rq.address;
            rs.value = static_cast<std::uint16_t>(rq.registers.size());
            break;
        case kReadDeviceIdentification: {
            auto all = bank.identification();
            if (all.empty()) {
                ex = 1;
                break;
            }
            // De base : les objets 0 a 2 ; courante : 0 a 6.
            const std::uint8_t last = rq.idCode == 1 ? 2 : 6;
            for (auto& o : all)
                if (o.first <= last) rs.objects.push_back(std::move(o));
            break;
        }
        default:
            ex = 1;
            break;
    }
    if (ex) {
        rs.exception = static_cast<std::uint8_t>(ex);
        rs.bits.clear();
        rs.registers.clear();
        rs.objects.clear();
    }
    return rs;
}

// ----------------------------------------------------------------- serveur ---
Server::~Server() { stop(); }

bool Server::start(const std::string& bindAddress, int port, std::shared_ptr<Bank> bank, std::string* why) {
    stop();
    if (!bank) return fail(why, "rien \xC3\xA0 servir");
    if (!listener_.listen(bindAddress, port, why)) return false;
    port_ = listener_.localPort();
    bank_ = std::move(bank);
    stop_.store(false);
    running_.store(true);
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = Stats{};
    }
    thread_ = std::thread([this] { loop(); });
    return true;
}

void Server::stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    listener_.close();
    running_.store(false);
}

Server::Stats Server::stats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void Server::loop() {
    struct Peer {
        net::Socket  socket;
        std::string  name;
        Frame        buffer;
        bool         dead{false};
    };
    std::vector<Peer> peers;
    constexpr std::size_t kMaxClients = 16;
    while (!stop_.load()) {
        std::vector<const net::Socket*> all{&listener_};
        for (const auto& p : peers) all.push_back(&p.socket);
        const auto ready = net::waitReadable(all, 50);
        for (const auto i : ready) {
            if (i == 0) {
                std::string name;
                net::Socket s = listener_.accept(0, &name);
                if (!s.valid()) continue;
                if (peers.size() >= kMaxClients) continue;   // refuse : il se ferme en sortant d'ici
                Peer p;
                p.socket = std::move(s);
                p.name = name;
                peers.push_back(std::move(p));
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.lastClient = name;
                continue;
            }
            Peer& peer = peers[i - 1];
            std::uint8_t chunk[1024];
            const long n = peer.socket.receiveSome(chunk, sizeof chunk, 0);
            if (n < 0) {
                peer.dead = true;
                continue;
            }
            peer.buffer.insert(peer.buffer.end(), chunk, chunk + n);
            // Les trames completes du tampon, dans l'ordre.
            while (peer.buffer.size() >= 7) {
                const std::size_t len = static_cast<std::size_t>((peer.buffer[4] << 8) | peer.buffer[5]);
                if (len < 2 || len > 254) {
                    peer.dead = true;
                    break;
                }
                if (peer.buffer.size() < 6 + len) break;
                Request rq;
                int exception = 0;
                const bool readable = decodeRequest(peer.buffer.data(), 6 + len, rq, exception);
                peer.buffer.erase(peer.buffer.begin(), peer.buffer.begin() + static_cast<std::ptrdiff_t>(6 + len));
                if (!readable) {
                    peer.dead = true;
                    break;
                }
                Response rs;
                // Lot 17 : l'espion du cote serveur (un jumeau visible sur le vrai reseau).
                if (tapServer_.load() && tapOn().load(std::memory_order_relaxed)) {
                    Frame in;
                    const Frame asked = encode(rq);
                    in.assign(asked.begin(), asked.end());
                    tapFrame(false, listener_, peer.name, in);
                }
                if (exception) {
                    rs.transaction = rq.transaction;
                    rs.unit = rq.unit;
                    rs.function = rq.function;
                    rs.exception = static_cast<std::uint8_t>(exception);
                } else {
                    // Lot 17 : la memoire sait qui la lit et l'ecrit.
                    tServing = clientLabel(peer.name);
                    rs = serve(*bank_, rq);
                    tServing.clear();
                }
                {
                    std::lock_guard<std::mutex> lock(statsMutex_);
                    ++stats_.requests;
                    if (rs.exception) ++stats_.exceptions;
                }
                if (mute_.load()) continue;
                // Lot 17 : le temps de reponse d'un appareil (un jumeau). La detection
                // des zones et le scanner de la carte n'attendent pas : des milliers
                // de sondes a 8 ms feraient d'une detection une affaire de minutes.
                const auto tool = [&] {
                    const std::string who = clientLabel(peer.name);
                    return who.rfind("Scanner", 0) == 0 || who.rfind("D\xC3\xA9tection", 0) == 0;
                };
                if (const int d = delayMs_.load(); (d > 0 || jitterMs_.load() > 0) && !tool()) {
                    const int j = jitterMs_.load();
                    const int wait = std::max(0, d + (j > 0 ? static_cast<int>(rq.transaction % static_cast<unsigned>(2 * j + 1)) - j : 0));
                    std::this_thread::sleep_for(std::chrono::milliseconds(wait));
                }
                const Frame out = encode(rs);
                if (tapServer_.load() && tapOn().load(std::memory_order_relaxed)) tapFrame(true, listener_, peer.name, out);
                if (!peer.socket.sendAll(out.data(), out.size(), 1000)) {
                    peer.dead = true;
                    break;
                }
            }
        }
        std::erase_if(peers, [](const Peer& p) { return p.dead || !p.socket.valid(); });
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.clients = peers.size();
    }
    peers.clear();
}

} // namespace hmi::modbus
