// =============================================================================
//  deploy/Frame.cpp - 1.28.0 : les trames (voir Frame.hpp)
// =============================================================================
#include "Frame.hpp"

namespace deploy {

namespace {

void putU32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

std::uint32_t getU32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
           | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

// Le corps (apres la longueur totale) : la longueur de l'en-tete, l'en-tete, les donnees.
bool decodeBody(std::string_view body, Frame& out, std::string* why) {
    if (body.size() < 4) return fail(why, "trame trop courte");
    const std::uint32_t hlen = getU32(reinterpret_cast<const unsigned char*>(body.data()));
    if (hlen > body.size() - 4) return fail(why, "longueur d'en-t\xC3\xAAte incoh\xC3\xA9rente");
    std::string err;
    core::json::Value header;
    if (!core::json::parse(body.substr(4, hlen), header, &err)) return fail(why, "en-t\xC3\xAAte JSON illisible (" + err + ")");
    if (!header.isObject()) return fail(why, "l'en-t\xC3\xAAte n'est pas un objet JSON");
    out.header = std::move(header);
    out.data.assign(body.substr(4 + hlen));
    return true;
}

} // namespace

bool encodeFrame(const core::json::Value& header, std::string_view data, std::string& out, std::string* why) {
    const std::string h = core::json::write(header, false);
    const std::uint64_t total = 4ull + h.size() + data.size();
    if (total > kMaxFrame)
        return fail(why, "trame trop grosse (" + std::to_string(total) + " octets, " + std::to_string(kMaxFrame) + " au plus)");
    out.clear();
    out.reserve(static_cast<std::size_t>(total) + 4);
    putU32(out, static_cast<std::uint32_t>(total));
    putU32(out, static_cast<std::uint32_t>(h.size()));
    out += h;
    out.append(data.data(), data.size());
    return true;
}

bool decodeFrame(std::string_view bytes, Frame& out, std::string* why) {
    if (bytes.size() < 8) return fail(why, "trame trop courte");
    const std::uint32_t total = getU32(reinterpret_cast<const unsigned char*>(bytes.data()));
    if (total > kMaxFrame) return fail(why, "trame trop grosse");
    if (total != bytes.size() - 4) return fail(why, "longueur totale incoh\xC3\xA9rente");
    return decodeBody(bytes.substr(4), out, why);
}

bool writeFrame(TlsStream& s, const core::json::Value& header, std::string_view data, int timeoutMs, std::string* why) {
    std::string bytes;
    if (!encodeFrame(header, data, bytes, why)) return false;
    return s.write(bytes.data(), bytes.size(), timeoutMs, why) == TlsStream::Io::Ok;
}

FrameRead readFrame(TlsStream& s, Frame& out, int idleMs, int timeoutMs, std::string* why, std::uint32_t* announced) {
    unsigned char len[4];
    switch (s.read(len, 4, idleMs, why)) {
        case TlsStream::Io::Ok: break;
        case TlsStream::Io::Closed: return FrameRead::Closed;
        case TlsStream::Io::Timeout: return FrameRead::Idle;
        case TlsStream::Io::Cancelled: return FrameRead::Cancelled;
        case TlsStream::Io::Error: return FrameRead::Error;
    }
    const std::uint32_t total = getU32(len);
    if (announced) *announced = total;
    if (total > kMaxFrame) {
        (void)fail(why, "trame trop grosse (" + std::to_string(total) + " octets annonc\xC3\xA9s, " + std::to_string(kMaxFrame) + " au plus)");
        return FrameRead::TooBig;
    }
    if (total < 4) {
        (void)fail(why, "trame trop courte");
        return FrameRead::Malformed;
    }
    std::string body(total, '\0');
    switch (s.read(body.data(), body.size(), timeoutMs, why)) {
        case TlsStream::Io::Ok: break;
        case TlsStream::Io::Closed: return FrameRead::Closed;
        case TlsStream::Io::Timeout: return FrameRead::Timeout;
        case TlsStream::Io::Cancelled: return FrameRead::Cancelled;
        case TlsStream::Io::Error: return FrameRead::Error;
    }
    return decodeBody(body, out, why) ? FrameRead::Ok : FrameRead::Malformed;
}

} // namespace deploy
