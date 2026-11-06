// =============================================================================
//  deploy/Client.cpp - 1.28.0 : le cote IHM Studio (voir Client.hpp)
// =============================================================================
#include "Client.hpp"

#include "Package.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

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
    if (!r.ok) {
        // Version 2 : le detail d'un refus (le retour automatique d'un START...).
        core::json::Value detail;
        if (!reply.data.empty() && core::json::parse(reply.data, detail, nullptr) && detail.isObject()) result = std::move(detail);
        return failure(r.error.empty() ? std::string(protocol::err::kInternal) : r.error, r.message);
    }
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
    for (const auto& a : r.at("apps").items()) {
        InstalledApp app;
        app.name = a.str("name");
        app.version = a.str("version");
        app.running = a.flag("running");
        app.pid = static_cast<int>(a.integer("pid"));
        app.versionCount = static_cast<int>(a.integer("versions"));
        s.apps.push_back(std::move(app));
    }
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

// ======================================================================= 1.29.0 ===
namespace {

namespace err = protocol::err;
using core::json::Value;
using Clock = std::chrono::steady_clock;

std::int64_t since(Clock::time_point t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// Une coupure (la connexion, une reponse perdue) : on se reconnecte et on reprend.
bool cut(const Outcome& o) { return o.error == err::kNetwork || o.error == err::kBadReply; }

InstalledApp appFrom(const Value& a) {
    InstalledApp app;
    app.name = a.str("name");
    app.version = a.str("active", a.str("version"));
    app.activeLabel = a.str("activeLabel");
    app.previous = a.str("previous");
    app.autostart = a.flag("autostart");
    app.running = a.flag("running");
    app.pid = static_cast<int>(a.integer("pid"));
    app.runningVersion = a.str("runningVersion");
    for (const auto& v : a.at("versions").items()) {
        InstalledVersion iv;
        iv.id = v.str("id");
        iv.label = v.str("label");
        iv.sha256 = v.str("sha256");
        iv.installedAt = v.integer("installedAt");
        iv.from = v.str("from");
        iv.publisher = v.str("publisher");
        iv.files = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("files")));
        iv.bytes = static_cast<std::uint64_t>(std::max<std::int64_t>(0, v.integer("bytes")));
        iv.active = v.flag("active");
        app.versions.push_back(std::move(iv));
    }
    app.versionCount = static_cast<int>(app.versions.size());
    return app;
}

std::string pidText(int pid) { return "pid " + std::to_string(pid); }

} // namespace

std::string deployStepName(DeployStep step) {
    switch (step) {
        case DeployStep::Check: return "V\xC3\xA9rification du paquet";
        case DeployStep::Connect: return "Connexion";
        case DeployStep::Prepare: return "Pr\xC3\xA9paration";
        case DeployStep::Send: return "Envoi";
        case DeployStep::Verify: return "V\xC3\xA9rification sur le PC";
        case DeployStep::Install: return "Installation";
        case DeployStep::Stop: return "Arr\xC3\xAAt du poste";
        case DeployStep::Start: return "Lancement du poste";
        case DeployStep::Done: return "Termin\xC3\xA9";
    }
    return "?";
}

Outcome Client::requestLong(std::string_view op, const Value& args, Value& result, int timeoutMs) {
    const int saved = requestTimeoutMs_;
    requestTimeoutMs_ = std::max(saved, timeoutMs);
    Outcome o = request(op, args, result);
    requestTimeoutMs_ = saved;
    return o;
}

Outcome Client::openForDeploy(const PairedMachine& m, int timeoutMs) {
    Outcome o = connect(m, timeoutMs);
    if (!o) return o;
    AgentInfo info;
    o = identify(info);
    if (!o) {
        close();
        return o;
    }
    if (proto_ < protocol::kDeployProto) {
        close();
        return failure(err::kIncompatible, "L'agent de " + (m.name.empty() ? m.address : m.name) + " (XPG-Deployment-Agent " + info.version + ", protocole "
                                               + std::to_string(info.proto)
                                               + ") ne sait pas encore installer : mets-le \xC3\xA0 jour sur ce PC (celui d'IHM Studio 1.29.0 ou plus r\xC3\xA9" "cent).");
    }
    return success();
}

Outcome Client::prepare(const std::string& application, const std::string& label, std::uint64_t size, const std::string& sha256,
                        std::uint64_t contentSize, PrepareReply& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    Value args = Value::object();
    args.set("application", application);
    args.set("version", label);
    args.set("size", size);
    args.set("sha256", sha256);
    args.set("contentSize", contentSize);
    Value r;
    o = request(protocol::kPrepare, args, r);
    if (!o) return o;
    PrepareReply p;
    p.transfer = r.str("transfer");
    p.offset = static_cast<std::uint64_t>(std::max<std::int64_t>(0, r.integer("offset")));
    p.chunkSize = static_cast<std::uint32_t>(std::clamp<std::int64_t>(r.integer("chunkSize", protocol::kChunkSize), 1, protocol::kChunkSize));
    p.diskFree = r.integer("diskFree", -1);
    p.installed = r.flag("installed");
    p.version = r.str("version");
    out = std::move(p);
    return success();
}

Outcome Client::sendChunk(const std::string& transfer, std::uint64_t offset, std::string_view bytes, std::uint64_t& received,
                          const std::string& claimedSha) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    const std::string id = protocol::newOperationId();
    Value header = protocol::requestHeader(protocol::kSendChunk, id, nextSeq(), protocol::nowMs(), proto_);
    Value chunk = Value::object();
    chunk.set("transfer", transfer);
    chunk.set("offset", offset);
    chunk.set("size", static_cast<std::uint64_t>(bytes.size()));
    chunk.set("sha256", claimedSha.empty() ? crypto::sha256Hex(bytes) : claimedSha);
    header.set("chunk", std::move(chunk));
    Frame reply;
    o = exchange(header, std::string(bytes), reply);
    if (!o) return o;
    protocol::Reply r;
    std::string why;
    if (!protocol::parseReply(reply.header, r, why)) return failure(err::kBadReply, why);
    if (!r.ok) return failure(r.error.empty() ? std::string(err::kInternal) : r.error, r.message);
    if (r.re != id) return failure(err::kBadReply, "r\xC3\xA9ponse \xC3\xA0 une autre demande (" + r.re + ")");
    Value result;
    if (!core::json::parse(reply.data, result, &why) || !result.isObject()) return failure(err::kBadReply, "r\xC3\xA9ponse de l'agent illisible : " + why);
    received = static_cast<std::uint64_t>(std::max<std::int64_t>(0, result.integer("received")));
    return success();
}

Outcome Client::verifyPackage(const std::string& transfer, Value& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    Value args = Value::object();
    args.set("transfer", transfer);
    return requestLong(protocol::kVerifyPackage, args, out, 10 * 60 * 1000);
}

Outcome Client::install(const std::string& transfer, std::optional<bool> autostart, Value& out, const std::string& application) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    Value args = Value::object();
    args.set("transfer", transfer);
    if (!application.empty()) args.set("application", application);
    if (autostart) args.set("autostart", *autostart);
    return requestLong(protocol::kInstall, args, out, 10 * 60 * 1000);
}

Outcome Client::stopStation(const Value& args, Value& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    const auto delay = std::clamp<std::int64_t>(args.integer("delai", 60), 0, 3600);
    return requestLong(protocol::kStop, args, out, static_cast<int>((delay + 120) * 1000));
}

Outcome Client::startStation(const Value& args, Value& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    const auto check = std::clamp<std::int64_t>(args.integer("check", 8), 1, 300);
    return requestLong(protocol::kStart, args, out, static_cast<int>((2 * check + 60) * 1000));
}

Outcome Client::rollbackRequest(const Value& args, Value& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    const auto delay = std::clamp<std::int64_t>(args.integer("delai", 60), 0, 3600);
    const auto check = std::clamp<std::int64_t>(args.integer("check", 8), 1, 300);
    return requestLong(protocol::kRollback, args, out, static_cast<int>((delay + 2 * check + 150) * 1000));
}

Outcome Client::listApps(AppList& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    Value r;
    o = request(protocol::kListApps, Value::object(), r);
    if (!o) return o;
    AppList l;
    for (const auto& a : r.at("apps").items()) l.apps.push_back(appFrom(a));
    const Value& st = r.at("station");
    l.station.running = st.flag("running");
    l.station.pid = static_cast<int>(st.integer("pid"));
    l.station.application = st.str("application");
    l.station.version = st.str("version");
    l.station.startedAt = st.integer("startedAt");
    l.station.adopted = st.flag("adopted");
    l.program = r.str("program");
    out = std::move(l);
    return success();
}

Outcome Client::history(int limit, std::vector<HistoryEntry>& out) {
    Outcome o = ensureIdentified();
    if (!o) return o;
    Value args = Value::object();
    args.set("limit", std::max(1, limit));
    Value r;
    o = request(protocol::kGetHistory, args, r);
    if (!o) return o;
    out.clear();
    for (const auto& e : r.at("entries").items()) {
        HistoryEntry h;
        h.at = e.integer("at");
        h.op = e.str("op");
        h.application = e.str("application");
        h.version = e.str("version");
        h.label = e.str("label");
        h.from = e.str("from");
        h.address = e.str("address");
        h.ok = e.flag("ok");
        h.result = e.str("result");
        h.message = e.str("message");
        out.push_back(std::move(h));
    }
    return success();
}

Outcome Client::listApps(const PairedMachine& m, AppList& out) {
    Outcome o = openForDeploy(m, 5000);
    if (o) o = listApps(out);
    close();
    return o;
}

Outcome Client::history(const PairedMachine& m, int limit, std::vector<HistoryEntry>& out) {
    Outcome o = openForDeploy(m, 5000);
    if (o) o = history(limit, out);
    close();
    return o;
}

Outcome Client::rollback(const PairedMachine& m, RollbackResult& out, const RollbackOptions& opt) {
    Outcome o = openForDeploy(m, opt.connectTimeoutMs);
    if (!o) return o;
    Value args = Value::object();
    if (!opt.application.empty()) args.set("application", opt.application);
    if (!opt.version.empty()) args.set("version", opt.version);
    args.set("delai", std::clamp(opt.stopDelaySeconds, 0, 3600));
    args.set("force", opt.force);
    if (!opt.stopReason.empty()) args.set("raison", opt.stopReason);
    if (opt.startCheckSeconds > 0) args.set("check", opt.startCheckSeconds);
    Value r;
    o = rollbackRequest(args, r);
    close();
    RollbackResult rr;
    rr.application = r.str("application");
    rr.from = r.str("from");
    rr.to = r.str("to");
    rr.wasRunning = r.flag("wasRunning");
    rr.restarted = r.flag("restarted");
    rr.running = r.flag("running");
    rr.pid = static_cast<int>(r.integer("pid"));
    rr.message = o ? r.str("message") : o.message;
    if (o) rr.message = "Retour en arri\xC3\xA8re sur " + (m.name.empty() ? m.address : m.name) + " : " + rr.message + ".";
    out = std::move(rr);
    if (o) o.message = out.message;
    return o;
}

DeployResult Client::deploy(const PairedMachine& m, const std::string& path, const DeployOptions& opt, const DeployProgress& progress) {
    DeployResult res;
    const std::string machine = m.name.empty() ? m.address : m.name;
    DeployStep current = DeployStep::Check;
    Clock::time_point stepStart = Clock::now();
    bool committed = false;   // INSTALL commence : on ne s'arrete plus
    const auto begin = [&](DeployStep s) {
        current = s;
        stepStart = Clock::now();
    };
    const auto done = [&](bool ok, bool skipped, std::string msg) { res.steps.push_back({current, ok, skipped, std::move(msg), since(stepStart)}); };
    // Faux : l'utilisateur annule (pris en compte jusqu'a INSTALL).
    const auto keepGoing = [&](DeployStep s, std::uint64_t b, std::uint64_t t) {
        if (!progress) return true;
        const bool go = progress(s, b, t);
        return go || committed;
    };
    const auto finish = [&](Outcome o) {
        close();
        res.outcome = std::move(o);
        if (progress) (void)progress(DeployStep::Done, 0, 0);
        return res;
    };
    const auto cancelOut = [&] {
        res.cancelled = true;
        done(false, false, "annul\xC3\xA9");
        const bool partial = res.bytesSent > 0 || res.resumedFrom > 0;
        return finish(failure(err::kCancelled, std::string("D\xC3\xA9ploiement annul\xC3\xA9 : rien n'a \xC3\xA9t\xC3\xA9 install\xC3\xA9")
                                                   + (partial ? " ; l'envoi reprendra o\xC3\xB9 il s'est arr\xC3\xAAt\xC3\xA9 au prochain d\xC3\xA9ploiement." : ".")));
    };

    // ---- 1. le paquet, ici : signe, intact ; son SHA-256 ----
    begin(DeployStep::Check);
    if (!keepGoing(DeployStep::Check, 0, 0)) return cancelOut();
    bool userCancel = false;
    PackageVerifyOptions vo;
    vo.inspect = true;
    vo.progress = [&](const PackageProgress& p) {
        if (keepGoing(DeployStep::Check, p.bytesDone, p.bytesTotal)) return true;
        userCancel = true;
        return false;
    };
    const PackageReport rep = deploy::verifyPackage(path, vo);
    if (userCancel) return cancelOut();
    if (rep.verdict != PackageVerdict::Accepted && rep.verdict != PackageVerdict::UnapprovedPublisher) {
        done(false, false, rep.reason);
        return finish(failure(err::kLocalPackage, rep.reason));
    }
    const PackageManifest& man = rep.manifest;
    res.application = man.application;
    res.label = man.appVersion;
    std::string sha, why;
    std::uint64_t size = 0;
    if (!crypto::sha256File(path, sha, &size, &why)) {
        done(false, false, why);
        return finish(failure(err::kLocalPackage, "le paquet ne se relit pas : " + why));
    }
    res.packageBytes = size;
    done(true, false, man.application + " " + man.appVersion + " : sign\xC3\xA9 et intact, " + std::to_string(man.files.size()) + " fichiers, "
                          + packageSizeText(size));

    // ---- 2. la connexion (epinglee, version 2) ----
    begin(DeployStep::Connect);
    if (!keepGoing(DeployStep::Connect, 0, 0)) return cancelOut();
    Outcome o = openForDeploy(m, opt.connectTimeoutMs);
    if (!o) {
        done(false, false, o.message);
        return finish(o);
    }
    done(true, false, "connect\xC3\xA9 \xC3\xA0 " + machine + " (" + m.address + ")");

    // ---- 3. PREPARE ----
    begin(DeployStep::Prepare);
    if (!keepGoing(DeployStep::Prepare, 0, 0)) return cancelOut();
    PrepareReply pr;
    o = prepare(man.application, man.appVersion, size, sha, man.totalSize, pr);
    if (!o) {
        done(false, false, o.message);
        return finish(o);
    }
    res.resumedFrom = pr.installed ? 0 : pr.offset;
    done(true, false,
         pr.installed ? "d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e sur ce PC (" + pr.version + ") : rien \xC3\xA0 envoyer"
         : pr.offset  ? "reprise : l'agent a d\xC3\xA9j\xC3\xA0 " + packageSizeText(pr.offset) + " sur " + packageSizeText(size)
                      : "place libre sur ce PC : " + (pr.diskFree >= 0 ? packageSizeText(static_cast<std::uint64_t>(pr.diskFree)) : std::string("?")));

    if (!pr.installed) {
        // ---- 4. SEND_CHUNK : la reprise apres une coupure ----
        begin(DeployStep::Send);
        std::ifstream in(P(path), std::ios::binary);
        std::uint64_t offset = pr.offset;
        const std::uint32_t chunk = pr.chunkSize ? pr.chunkSize : protocol::kChunkSize;
        std::string buf(chunk, '\0');
        int failures = 0;
        while (offset < size) {
            if (!keepGoing(DeployStep::Send, offset, size)) return cancelOut();
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, size - offset));
            in.clear();
            in.seekg(static_cast<std::streamoff>(offset));
            in.read(buf.data(), static_cast<std::streamsize>(n));
            if (!in || static_cast<std::size_t>(in.gcount()) != n) {
                done(false, false, "le paquet ne se relit pas");
                return finish(failure(err::kLocalPackage, "le paquet " + path + " ne se relit pas (il a chang\xC3\xA9 ou disparu pendant l'envoi)."));
            }
            std::uint64_t received = 0;
            o = sendChunk(pr.transfer, offset, std::string_view(buf.data(), n), received);
            if (o) {
                if (received > offset) res.bytesSent += received - offset;
                offset = std::max(offset, received);
                continue;
            }
            if (!cut(o) && o.error != err::kTransfer && o.error != err::kBadChunk) {
                done(false, false, o.message);
                return finish(o);
            }
            // Une coupure : se reconnecter, demander a l'agent ou il en est (PREPARE), reprendre.
            bool resumed = false;
            while (!resumed) {
                if (++failures > opt.retries) {
                    const std::string m2 = "l'envoi a \xC3\xA9t\xC3\xA9 coup\xC3\xA9 " + std::to_string(failures - 1) + " fois (" + o.message
                                           + ") : il reprendra o\xC3\xB9 il en est au prochain d\xC3\xA9ploiement.";
                    done(false, false, m2);
                    return finish(failure(o.error, "D\xC3\xA9ploiement interrompu sur " + machine + " : " + m2));
                }
                if (cut(o) || !connected()) {
                    ++res.reconnects;
                    std::this_thread::sleep_for(std::chrono::milliseconds(300 * failures));
                    o = openForDeploy(m, opt.connectTimeoutMs);
                    if (!o) continue;
                }
                o = prepare(man.application, man.appVersion, size, sha, man.totalSize, pr);
                if (!o) {
                    if (cut(o)) continue;
                    done(false, false, o.message);
                    return finish(o);
                }
                offset = pr.offset;
                resumed = true;
            }
        }
        (void)keepGoing(DeployStep::Send, size, size);
        std::string sent = packageSizeText(res.bytesSent) + " envoy\xC3\xA9s";
        if (res.resumedFrom) sent += " (l'agent avait d\xC3\xA9j\xC3\xA0 " + packageSizeText(res.resumedFrom) + ")";
        if (res.reconnects) sent += ", " + std::to_string(res.reconnects) + " reprise(s) apr\xC3\xA8s une coupure";
        done(true, false, sent);

        // ---- 5. VERIFY_PACKAGE ----
        begin(DeployStep::Verify);
        if (!keepGoing(DeployStep::Verify, 0, 0)) return cancelOut();
        Value v;
        o = verifyPackage(pr.transfer, v);
        if (!o) {
            done(false, false, o.message);
            return finish(failure(o.error, "Le paquet est refus\xC3\xA9 par " + machine + " : " + o.message));
        }
        done(true, false, v.str("reason"));
    } else {
        begin(DeployStep::Send);
        done(true, true, "rien \xC3\xA0 envoyer");
        begin(DeployStep::Verify);
        done(true, true, "d\xC3\xA9j\xC3\xA0 v\xC3\xA9rifi\xC3\xA9 \xC3\xA0 l'installation");
    }

    // ---- 6. INSTALL (ensuite, on va au bout) ----
    begin(DeployStep::Install);
    if (!keepGoing(DeployStep::Install, 0, 0)) return cancelOut();
    committed = true;
    Value iv;
    o = install(pr.transfer, opt.autostart, iv, res.application);
    if (!o) {
        done(false, false, o.message);
        return finish(failure(o.error, "Installation impossible sur " + machine + " : " + o.message));
    }
    res.version = iv.str("version");
    res.previousVersion = iv.str("previous");
    res.alreadyInstalled = iv.flag("already");
    for (const auto& r : iv.at("removed").items()) res.removedVersions.push_back(r.asString());
    done(true, false, iv.str("message"));

    // ---- 7. STOP (si un poste tourne) ----
    begin(DeployStep::Stop);
    (void)keepGoing(DeployStep::Stop, 0, 0);
    AppList apps;
    o = listApps(apps);
    if (!o) {
        done(false, false, o.message);
        return finish(failure(o.error, res.application + " " + res.version + " est install\xC3\xA9" "e sur " + machine + ", mais l'\xC3\xA9tat du poste ne se lit pas : " + o.message));
    }
    if (apps.station.running && apps.station.application == res.application && apps.station.version == res.version) {
        res.started = true;
        res.pid = apps.station.pid;
        done(true, true, "le poste tourne d\xC3\xA9j\xC3\xA0 sur " + res.version);
        begin(DeployStep::Start);
        done(true, true, "d\xC3\xA9j\xC3\xA0 en marche (" + pidText(res.pid) + ")");
        return finish(Outcome{true, {}, res.application + " " + res.version + " est d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e et en marche sur " + machine + " : rien \xC3\xA0 refaire."});
    }
    if (!apps.station.running) {
        done(true, true, "aucun poste en marche");
    } else {
        Value args = Value::object();
        args.set("raison", opt.stopReason.empty() ? "Mise \xC3\xA0 jour vers " + res.application + " " + res.label + " demand\xC3\xA9" "e depuis " + name_ : opt.stopReason);
        args.set("delai", std::clamp(opt.stopDelaySeconds, 0, 3600));
        args.set("force", opt.force);
        Value sv;
        o = stopStation(args, sv);
        if (!o) {
            done(false, false, o.message);
            return finish(failure(o.error, res.application + " " + res.version + " est install\xC3\xA9" "e et active sur " + machine
                                               + ", mais le poste en marche ne s'est pas arr\xC3\xAAt\xC3\xA9 : " + o.message));
        }
        res.stationWasRunning = true;
        res.stationForced = sv.flag("forced");
        done(true, false, apps.station.application + " " + apps.station.version + " (" + pidText(apps.station.pid) + ") "
                              + (res.stationForced ? "termin\xC3\xA9 de force" : "a quitt\xC3\xA9 proprement"));
    }

    // ---- 8. START, et le controle de demarrage ----
    begin(DeployStep::Start);
    (void)keepGoing(DeployStep::Start, 0, 0);
    Value args = Value::object();
    args.set("application", res.application);
    if (opt.startCheckSeconds > 0) args.set("check", opt.startCheckSeconds);
    Value st;
    o = startStation(args, st);
    res.pid = static_cast<int>(st.integer("pid"));
    res.started = st.flag("running");
    if (!o) {
        res.rolledBack = st.flag("rolledBack");
        res.rolledBackTo = st.str("rolledBackTo");
        done(false, false, o.message);
        return finish(failure(o.error, "Sur " + machine + " : " + o.message));
    }
    done(true, false, "en marche (" + pidText(res.pid) + ")");
    std::string msg = res.application + " " + res.version + (res.alreadyInstalled ? " \xC3\xA9tait d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e" : " est install\xC3\xA9" "e")
                      + " sur " + machine + " et le poste est en marche (" + pidText(res.pid) + ").";
    if (!res.previousVersion.empty() && res.previousVersion != res.version)
        msg += " Version pr\xC3\xA9" "c\xC3\xA9" "dente : " + res.previousVersion + " (Retour en arri\xC3\xA8re pour y revenir).";
    if (res.resumedFrom) msg += " L'envoi a repris o\xC3\xB9 il s'\xC3\xA9tait arr\xC3\xAAt\xC3\xA9 (" + packageSizeText(res.resumedFrom) + " d\xC3\xA9j\xC3\xA0 re\xC3\xA7us).";
    if (res.stationForced) msg += " L'ancien poste a \xC3\xA9t\xC3\xA9 termin\xC3\xA9 de force.";
    return finish(Outcome{true, {}, msg});
}

} // namespace deploy
