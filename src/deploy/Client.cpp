// =============================================================================
//  deploy/Client.cpp - 1.28.0 : le cote IHM Studio (voir Client.hpp)
// =============================================================================
#include "Client.hpp"

#include <algorithm>

namespace deploy {

namespace {

Outcome failure(std::string_view code, std::string message) {
    Outcome o;
    o.ok = false;
    o.error = std::string(code);
    o.message = std::move(message);
    return o;
}

Outcome success() {
    Outcome o;
    o.ok = true;
    return o;
}

std::string readable(const std::string& hexFp) {
    crypto::Bytes b;
    if (!crypto::unhex(hexFp, b, 32)) return "?";
    crypto::Digest d{};
    std::copy(b.begin(), b.end(), d.begin());
    return crypto::fingerprintText(d, 4);
}

} // namespace

std::string pinMismatchMessage(const std::string& expectedHex, const std::string& presentedHex) {
    return "Ce n'est plus la m\xC3\xAAme machine, ou quelqu'un s'interpose : l'empreinte pr\xC3\xA9sent\xC3\xA9" "e ("
           + readable(presentedHex) + ") n'est pas celle gard\xC3\xA9" "e \xC3\xA0 l'appairage (" + readable(expectedHex)
           + "). Connexion refus\xC3\xA9" "e. Si ce PC a \xC3\xA9t\xC3\xA9 r\xC3\xA9install\xC3\xA9 ou son agent r\xC3\xA9initialis\xC3\xA9, "
             "retire-le de la liste et appaire-le de nouveau.";
}

Client::Client(TlsIdentity me, std::string studioName, std::string publisherKeyHex)
    : me_(std::move(me)), name_(std::move(studioName)), keyHex_(std::move(publisherKeyHex)) {}

Client::~Client() { close(); }

void Client::close() {
    tls_.close();
    proto_ = 0;
}

bool Client::connected() const { return tls_.open(); }

std::string Client::peerFingerprint() const { return tls_.peerFingerprintHex(); }

Outcome Client::connect(const std::string& address, int port, int timeoutMs, const std::string& pinned) {
    close();
    tls_ = TlsStream();
    address_ = address;
    port_ = port;
    seq_ = 0;
    proto_ = 0;
    info_ = AgentInfo{};
    std::string why;
    if (!tls_.connect(address, port, me_, timeoutMs, &why))
        return failure(protocol::err::kNetwork, "connexion \xC3\xA0 " + address + ":" + std::to_string(port) + " impossible : " + why);
    if (!pinned.empty() && tls_.peerFingerprintHex() != pinned) {
        const std::string presented = tls_.peerFingerprintHex();
        tls_.close();
        return failure(protocol::err::kPinMismatch, pinMismatchMessage(pinned, presented));
    }
    return success();
}

Outcome Client::connect(const PairedMachine& m, int timeoutMs) { return connect(m.address, m.port, timeoutMs, m.fingerprint); }

Outcome Client::exchange(const core::json::Value& header, const std::string& data, Frame& reply) {
    if (!tls_.open()) return failure(protocol::err::kNetwork, "pas de connexion avec l'agent");
    std::string why;
    if (!writeFrame(tls_, header, data, requestTimeoutMs_, &why)) {
        const bool tooBig = why.rfind("trame trop grosse", 0) == 0;
        if (!tooBig) close();
        return failure(tooBig ? protocol::err::kTooBig : protocol::err::kNetwork,
                       tooBig ? why : "la connexion avec l'agent est coup\xC3\xA9" "e (" + why + ")");
    }
    const FrameRead rd = readFrame(tls_, reply, requestTimeoutMs_, requestTimeoutMs_, &why);
    if (rd == FrameRead::Ok) return success();
    close();
    if (rd == FrameRead::Idle || rd == FrameRead::Timeout)
        return failure(protocol::err::kNetwork, "l'agent ne r\xC3\xA9pond pas (" + std::to_string(requestTimeoutMs_ / 1000) + " s)");
    if (rd == FrameRead::TooBig || rd == FrameRead::Malformed) return failure(protocol::err::kBadReply, "r\xC3\xA9ponse de l'agent illisible : " + why);
    return failure(protocol::err::kNetwork, "la connexion avec l'agent est coup\xC3\xA9" "e (" + why + ")");
}

Outcome Client::request(std::string_view op, const core::json::Value& args, core::json::Value& result, const std::string& opId) {
    const std::string id = opId.empty() ? protocol::newOperationId() : opId;
    const auto header = protocol::requestHeader(op, id, nextSeq(), protocol::nowMs(), proto_ ? proto_ : 1);
    Frame reply;
    Outcome o = exchange(header, args.isNull() ? std::string() : core::json::write(args, false), reply);
    if (!o) return o;
    protocol::Reply r;
    std::string why;
    if (!protocol::parseReply(reply.header, r, why)) return failure(protocol::err::kBadReply, why);
    if (!r.ok) return failure(r.error.empty() ? std::string(protocol::err::kInternal) : r.error, r.message);
    if (r.re != id) return failure(protocol::err::kBadReply, "r\xC3\xA9ponse \xC3\xA0 une autre demande (" + r.re + ")");
    result = core::json::Value::object();
    if (!reply.data.empty() && (!core::json::parse(reply.data, result, &why) || !result.isObject()))
        return failure(protocol::err::kBadReply, "r\xC3\xA9ponse de l'agent illisible : " + why);
    return success();
}

Outcome Client::identify(AgentInfo& out) {
    core::json::Value args = core::json::Value::object();
    core::json::Value protos = core::json::Value::array();
    for (const int p : offered_.empty() ? protocol::acceptedVersions() : offered_) protos.push(p);
    args.set("protos", std::move(protos));
    args.set("machine", name_);
    args.set("app", "IHM Studio");
    core::json::Value r;
    Outcome o = request(protocol::kIdentify, args, r);
    if (!o) return o;
    AgentInfo info;
    info.proto = static_cast<int>(r.integer("proto"));
    for (const auto& p : r.at("protos").items()) info.protos.push_back(static_cast<int>(p.asInt()));
    info.machine = r.str("machine");
    info.id = r.str("id");
    info.version = r.str("version");
    info.paired = r.flag("paired");
    info.fingerprint = tls_.peerFingerprintHex();
    const auto& mine = offered_.empty() ? protocol::acceptedVersions() : offered_;
    if (std::find(mine.begin(), mine.end(), info.proto) == mine.end())
        return failure(protocol::err::kIncompatible, "l'agent a choisi la version " + std::to_string(info.proto) + ", que ce Studio ne parle pas");
    proto_ = info.proto;
    info_ = info;
    out = info;
    return success();
}

Outcome Client::ensureIdentified() {
    if (proto_ != 0) return success();
    AgentInfo info;
    return identify(info);
}

Outcome Client::pair(const std::string& rawCode, PairedMachine& out) {
    std::string code;
    for (const char c : rawCode)
        if (c != ' ' && c != '-' && c != '\t') code.push_back(c);
    if (code.size() != static_cast<std::size_t>(pairing::kCodeDigits)
        || !std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return failure(protocol::err::kBadArgs, "le code d'appairage a 6 chiffres (affich\xC3\xA9 par l'agent : XPG-Deployment-Agent --appairer)");
    Outcome o = ensureIdentified();
    if (!o) return o;
    // L'empreinte de l'agent : celle de CETTE poignee de main TLS (un intermediaire en aurait une autre).
    const crypto::Digest agentFp = forceAgentFp_ ? forcedAgentFp_ : tls_.peerFingerprint();
    pairing::StudioPairing sp(code, me_.fingerprint(), agentFp, name_, keyHex_);
    core::json::Value start, reply, finish, done;
    std::string why;
    if (!sp.start(start, &why)) return failure(protocol::err::kBadArgs, why);
    o = request(protocol::kPairStart, start, reply);
    if (!o) return o;
    if (!sp.answer(reply, finish, &why)) {
        core::json::Value abandon = core::json::Value::object();
        abandon.set("abandon", true);
        (void)request(protocol::kPairFinish, abandon, done);   // l'agent compte l'essai manque tout de suite
        return failure(protocol::err::kWrongCode, why);
    }
    o = request(protocol::kPairFinish, finish, done);
    if (!o) return o;
    PairedMachine m;
    m.fingerprint = tls_.peerFingerprintHex();
    m.name = sp.agentName();
    m.id = machineIdOfHex(m.fingerprint);
    m.address = address_;
    m.port = port_;
    m.pairedAt = protocol::nowMs();
    out = m;
    return success();
}

Outcome Client::status(AgentStatus& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    core::json::Value r;
    o = request(protocol::kStatus, core::json::Value::object(), r);
    if (!o) return o;
    AgentStatus s;
    s.machine = r.str("machine");
    s.id = r.str("id");
    s.version = r.str("version");
    s.system = r.str("system");
    s.startedAt = r.integer("startedAt");
    s.uptimeS = r.integer("uptimeS");
    s.diskFree = r.integer("diskFree", -1);
    s.pairedCount = static_cast<int>(r.integer("pairedCount"));
    for (const auto& a : r.at("apps").items()) s.apps.push_back({a.str("name"), a.str("version")});
    out = std::move(s);
    return success();
}

Outcome Client::logs(int lines, std::vector<std::string>& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    core::json::Value args = core::json::Value::object();
    args.set("lines", std::max(1, lines));
    core::json::Value r;
    o = request(protocol::kGetLogs, args, r);
    if (!o) return o;
    out.clear();
    for (const auto& l : r.at("lines").items()) out.push_back(l.asString());
    return success();
}

Outcome Client::unpair() {
    Outcome o = ensureIdentified();
    if (!o) return o;
    core::json::Value r;
    return request(protocol::kUnpair, core::json::Value::object(), r);
}

} // namespace deploy
