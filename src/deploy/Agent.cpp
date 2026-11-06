// =============================================================================
//  deploy/Agent.cpp - 1.28.0 : le coeur de l'agent (voir Agent.hpp)
// =============================================================================
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

#include "Agent.hpp"

#include "Discovery.hpp"
#include "Frame.hpp"
#include "Journal.hpp"
#include "Net.hpp"
#include "Pairing.hpp"
#include "Paths.hpp"
#include "Tls.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace deploy {

namespace {

namespace fs = std::filesystem;
fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

using Clock = std::chrono::steady_clock;

std::string shortFp(const crypto::Digest& d) { return crypto::fingerprintText(d, 2); }

int pid() {
#if defined(_WIN32)
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(::getpid());
#endif
}

// Une connexion : TLS, la version choisie, la garde contre le rejeu, l'essai d'appairage.
struct Conn {
    TlsStream tls;
    crypto::Digest peer{};
    std::string peerHex;
    std::string address;
    std::string studioName;   // annonce par IDENTIFY (pour le journal)
    int proto{0};             // 0 : IDENTIFY pas encore fait
    protocol::ReplayGuard guard;
    std::unique_ptr<pairing::AgentPairing> pairing;
    bool pairTried{false};
    bool pairPending{false};
};

struct Response {
    core::json::Value header;
    std::string data;
};

} // namespace

struct Agent::Impl {
    AgentConfig cfg;
    TlsIdentity identity;
    std::string name;
    std::string id;
    std::unique_ptr<Journal> journal;
    std::unique_ptr<pairing::StudioStore> studios;
    protocol::ResponseCache cache;
    std::mutex pairMutex;   // le fichier du code

    net::TcpSocket listener;
    net::UdpSocket udp;
    int tcpPort{0};
    int udpPort{-1};

    std::atomic<bool> stopping{false};
    std::atomic<bool> isRunning{false};
    std::atomic<int> active{0};
    std::int64_t startedAtMs{0};
    Clock::time_point startedAt{};

    std::thread acceptThread, udpThread, stateThread;
    std::mutex stateMutex;
    std::condition_variable stateCv;

    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex workersMutex;
    std::vector<Worker> workers;

    explicit Impl(AgentConfig c) : cfg(std::move(c)) {}

    // ------------------------------------------------------------ etat.json --
    void writeState(bool running) {
        core::json::Value v = core::json::Value::object();
        v.set("format", "xpg-agent-etat");
        v.set("version", 1);
        v.set("running", running);
        v.set("machine", name);
        v.set("id", id);
        v.set("agentVersion", cfg.version);
        v.set("pid", pid());
        v.set("port", tcpPort);
        v.set("discoveryPort", udpPort);
        v.set("startedAt", startedAtMs);
        v.set("updatedAt", protocol::nowMs());
        v.set("connections", active.load());
        v.set("paired", static_cast<std::int64_t>(studios ? studios->size() : 0));
        v.set("fingerprint", identity.fingerprintHex());
        const std::string text = core::json::write(v, true) + "\n";
        (void)crypto::writePrivateFile(joinPath(cfg.dir, "etat.json"), crypto::Bytes(text.begin(), text.end()), nullptr);
    }

    // ---------------------------------------------------------------- reponses --
    Response fail(const protocol::Request& r, int proto, std::string_view code, const std::string& message) {
        return {protocol::errorHeader(r.op, r.id, r.seq, code, message, proto), {}};
    }
    Response ok(const protocol::Request& r, int proto, const core::json::Value& data) {
        return {protocol::okHeader(r.op, r.id, r.seq, proto), core::json::write(data, false)};
    }

    void log(const std::string& opId, std::string_view op, const std::string& detail) { journal->write(opId, op, detail); }

    std::string who(const Conn& c) const {
        return (c.studioName.empty() ? std::string("?") : c.studioName) + " " + c.address + " " + shortFp(c.peer);
    }

    // Un appairage manque : apres 3 essais, le code s'efface.
    void pairFailed() {
        std::lock_guard<std::mutex> lock(pairMutex);
        pairing::CodeState cs;
        const auto r = pairing::loadCode(cfg.dir, cs, nullptr);
        if (r == pairing::CodeLoad::Exhausted || (r == pairing::CodeLoad::Ok && cs.attempts >= pairing::kMaxAttempts)
            || r == pairing::CodeLoad::Expired)
            pairing::eraseCode(cfg.dir);
    }

    Response identify(Conn& c, const protocol::Request& r, const core::json::Value& args) {
        std::vector<int> theirs;
        for (const auto& p : args.at("protos").items())
            if (p.isNumber()) theirs.push_back(static_cast<int>(p.asInt()));
        if (theirs.empty()) theirs.push_back(r.proto);
        c.studioName = args.str("machine").substr(0, 128);
        const int v = protocol::chooseVersion(protocol::acceptedVersions(), theirs);
        if (v == 0) {
            std::string list;
            for (const int t : theirs) list += (list.empty() ? "" : ", ") + std::to_string(t);
            log(r.id, r.op, "refus\xC3\xA9 : aucune version commune (Studio : " + list + ") " + who(c));
            return fail(r, 1, protocol::err::kIncompatible,
                        "aucune version de protocole commune : l'agent parle la version 1, IHM Studio la (les) version(s) " + list
                            + " - mets \xC3\xA0 jour l'un des deux");
        }
        c.proto = v;
        core::json::Value d = core::json::Value::object();
        d.set("proto", v);
        core::json::Value mine = core::json::Value::array();
        for (const int p : protocol::acceptedVersions()) mine.push(p);
        d.set("protos", std::move(mine));
        d.set("machine", name);
        d.set("id", id);
        d.set("version", cfg.version);
        d.set("paired", studios->contains(c.peerHex));
        log(r.id, r.op, who(c) + " proto " + std::to_string(v));
        return ok(r, v, d);
    }

    Response pairStart(Conn& c, const protocol::Request& r, const core::json::Value& args) {
        if (c.pairTried) {
            log(r.id, r.op, "refus\xC3\xA9 : d\xC3\xA9j\xC3\xA0 un essai sur cette connexion " + who(c));
            return fail(r, c.proto, protocol::err::kPairing, "un seul essai d'appairage par connexion : reconnecte-toi pour r\xC3\xA9" "essayer");
        }
        pairing::CodeState cs;
        {
            std::lock_guard<std::mutex> lock(pairMutex);
            std::string w;
            const auto st = pairing::loadCode(cfg.dir, cs, &w);
            const std::string again = " : lance \xC2\xAB XPG-Deployment-Agent --appairer \xC2\xBB sur ce PC pour avoir un nouveau code";
            if (st == pairing::CodeLoad::None) {
                log(r.id, r.op, "refus\xC3\xA9 : aucun code en cours " + who(c));
                return fail(r, c.proto, protocol::err::kNoCode, "aucun code d'appairage en cours" + again);
            }
            if (st == pairing::CodeLoad::Expired) {
                pairing::eraseCode(cfg.dir);
                log(r.id, r.op, "refus\xC3\xA9 : code expir\xC3\xA9 " + who(c));
                return fail(r, c.proto, protocol::err::kNoCode, "le code d'appairage a expir\xC3\xA9 (5 minutes)" + again);
            }
            if (st == pairing::CodeLoad::Exhausted) {
                pairing::eraseCode(cfg.dir);
                log(r.id, r.op, "refus\xC3\xA9 : code \xC3\xA9puis\xC3\xA9 " + who(c));
                return fail(r, c.proto, protocol::err::kNoCode, "le code d'appairage a \xC3\xA9t\xC3\xA9 effac\xC3\xA9 apr\xC3\xA8s 3 essais manqu\xC3\xA9s" + again);
            }
            if (st == pairing::CodeLoad::Error) {
                log(r.id, r.op, "refus\xC3\xA9 : " + w);
                return fail(r, c.proto, protocol::err::kInternal, w);
            }
            c.pairTried = true;   // un code en cours : c'est un essai (sans code, rien n'est consomme)
            ++cs.attempts;        // compte des maintenant : un essai abandonne est un essai
            if (!pairing::saveCode(cfg.dir, cs, &w)) {
                log(r.id, r.op, "refus\xC3\xA9 : " + w);
                return fail(r, c.proto, protocol::err::kInternal, w);
            }
        }
        c.pairing = std::make_unique<pairing::AgentPairing>(cs.code, c.peer, identity.fingerprint(), name, id);
        core::json::Value reply;
        std::string w;
        if (!c.pairing->start(args, reply, &w)) {
            pairFailed();
            log(r.id, r.op, "refus\xC3\xA9 : " + w + " " + who(c));
            return fail(r, c.proto, protocol::err::kBadArgs, w);
        }
        c.pairPending = true;
        log(r.id, r.op, "essai " + std::to_string(cs.attempts) + "/" + std::to_string(pairing::kMaxAttempts) + " "
                            + c.pairing->studioName() + " " + c.address + " " + shortFp(c.peer));
        return ok(r, c.proto, reply);
    }

    Response pairFinish(Conn& c, const protocol::Request& r, const core::json::Value& args) {
        if (!c.pairPending || !c.pairing) {
            log(r.id, r.op, "refus\xC3\xA9 : sans PAIR_START " + who(c));
            return fail(r, c.proto, protocol::err::kPairing, "PAIR_FINISH sans PAIR_START");
        }
        c.pairPending = false;
        std::string w;
        if (args.flag("abandon") || !c.pairing->finish(args, &w)) {
            if (w.empty()) w = "l'appairage a \xC3\xA9" "chou\xC3\xA9 : le code ne correspond pas (ou quelqu'un s'interpose entre les deux PC)";
            pairFailed();
            log(r.id, r.op, "\xC3\xA9" "chec : code faux ou interm\xC3\xA9" "diaire " + who(c));
            return fail(r, c.proto, protocol::err::kWrongCode, w);
        }
        pairing::PairedStudio s;
        s.fingerprint = c.peerHex;
        s.name = c.pairing->studioName();
        s.publisherKey = c.pairing->studioKeyHex();
        s.pairedAt = protocol::nowMs();
        {
            std::lock_guard<std::mutex> lock(pairMutex);
            pairing::eraseCode(cfg.dir);   // le code est consomme
        }
        if (!studios->upsert(s, &w)) {
            log(r.id, r.op, "\xC3\xA9" "chec : " + w);
            return fail(r, c.proto, protocol::err::kInternal, w);
        }
        c.pairing.reset();
        log(r.id, r.op, "appair\xC3\xA9 : " + s.name + " " + c.address + " " + shortFp(c.peer) + " \xC3\xA9" "diteur "
                            + s.publisherKey.substr(0, 16));
        core::json::Value d = core::json::Value::object();
        d.set("machine", name);
        d.set("id", id);
        return ok(r, c.proto, d);
    }

    Response status(Conn& c, const protocol::Request& r) {
        core::json::Value d = core::json::Value::object();
        d.set("machine", name);
        d.set("id", id);
        d.set("version", cfg.version);
        d.set("system", systemDescription());
        d.set("startedAt", startedAtMs);
        d.set("uptimeS", static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - startedAt).count()));
        std::error_code ec;
        const auto sp = fs::space(P(cfg.dir), ec);
        d.set("diskFree", ec ? std::int64_t(-1) : static_cast<std::int64_t>(sp.available));
        d.set("pairedCount", static_cast<std::int64_t>(studios->size()));
        d.set("apps", core::json::Value::array());   // 1.29.0 : les applications installees
        log(r.id, r.op, "ok " + who(c));
        return ok(r, c.proto, d);
    }

    Response logs(Conn& c, const protocol::Request& r, const core::json::Value& args) {
        const auto n = static_cast<std::size_t>(std::clamp<std::int64_t>(args.integer("lines", 100), 1, 1000));
        const auto lines = journal->tail(n);
        core::json::Value list = core::json::Value::array();
        for (const auto& l : lines) list.push(l);
        core::json::Value d = core::json::Value::object();
        d.set("lines", std::move(list));
        log(r.id, r.op, std::to_string(lines.size()) + " lignes " + who(c));
        return ok(r, c.proto, d);
    }

    Response unpair(Conn& c, const protocol::Request& r) {
        std::string w;
        if (!studios->remove(c.peerHex, &w)) {
            log(r.id, r.op, "\xC3\xA9" "chec : " + w);
            return fail(r, c.proto, protocol::err::kInternal, w);
        }
        log(r.id, r.op, "d\xC3\xA9sappair\xC3\xA9 : " + who(c));
        core::json::Value d = core::json::Value::object();
        d.set("unpaired", true);
        return ok(r, c.proto, d);
    }

    Response dispatch(Conn& c, const protocol::Request& r, const core::json::Value& args) {
        if (r.op == protocol::kPairStart) return pairStart(c, r, args);
        if (r.op == protocol::kPairFinish) return pairFinish(c, r, args);
        const bool known = r.op == protocol::kStatus || r.op == protocol::kGetLogs || r.op == protocol::kUnpair;
        if (!known) {
            log(r.id, r.op, "refus\xC3\xA9 : op\xC3\xA9ration inconnue " + who(c));
            return fail(r, c.proto, protocol::err::kUnknownOp, "op\xC3\xA9ration inconnue de cet agent : " + r.op);
        }
        if (!studios->contains(c.peerHex)) {
            log(r.id, r.op, "refus\xC3\xA9 : PC non appair\xC3\xA9 " + who(c));
            return fail(r, c.proto, protocol::err::kNotPaired,
                        "ce PC n'est pas appair\xC3\xA9 \xC3\xA0 l'agent (ou son certificat a chang\xC3\xA9) : appaire-le d'abord avec le code \xC3\xA0 6 chiffres");
        }
        if (r.op == protocol::kStatus) return status(c, r);
        if (r.op == protocol::kGetLogs) return logs(c, r, args);
        return unpair(c, r);
    }

    bool send(Conn& c, const Response& resp) {
        return writeFrame(c.tls, resp.header, resp.data, cfg.ioTimeoutMs, nullptr);
    }

    // ------------------------------------------------------------- une connexion --
    void serve(net::TcpSocket sock, const std::string& peer) {
        Conn c;
        c.tls.setCancel(&stopping);
        std::string why;
        if (!c.tls.accept(std::move(sock), identity, cfg.handshakeTimeoutMs, &why)) {
            if (!stopping) log("", "TLS", "refus\xC3\xA9 " + peer + " : " + why);
            return;
        }
        c.peer = c.tls.peerFingerprint();
        c.peerHex = crypto::hex(c.peer);
        c.address = c.tls.peerAddress();
        for (;;) {
            Frame f;
            std::uint32_t announced = 0;
            const FrameRead rd = readFrame(c.tls, f, cfg.idleTimeoutMs, cfg.ioTimeoutMs, &why, &announced);
            if (rd == FrameRead::TooBig) {
                log("", "TRAME", "refus\xC3\xA9" "e : " + std::to_string(announced) + " octets " + who(c));
                protocol::Request none;
                none.op = "?";
                (void)send(c, fail(none, c.proto ? c.proto : 1, protocol::err::kTooBig,
                                   "trame trop grosse (" + std::to_string(announced) + " octets annonc\xC3\xA9s, "
                                       + std::to_string(kMaxFrame) + " au plus) : connexion ferm\xC3\xA9" "e"));
                break;
            }
            if (rd == FrameRead::Malformed) {
                protocol::Request none;
                none.op = "?";
                if (!send(c, fail(none, c.proto ? c.proto : 1, protocol::err::kBadHeader, why))) break;
                continue;
            }
            if (rd != FrameRead::Ok) break;   // fermee, muette trop longtemps, arret, erreur
            protocol::Request r;
            std::string msg;
            if (!protocol::parseRequest(f.header, r, msg)) {
                protocol::Request bad;
                bad.op = f.header.str("op", "?");
                bad.id = f.header.str("id");
                if (!send(c, fail(bad, c.proto ? c.proto : 1, protocol::err::kBadHeader, "en-t\xC3\xAAte de demande invalide : " + msg))) break;
                continue;
            }
            std::string code;
            if (!c.guard.accept(r.seq, r.ts, protocol::nowMs(), code, msg)) {
                log(r.id, r.op, "refus\xC3\xA9 : " + code + " " + who(c));
                if (!send(c, fail(r, c.proto ? c.proto : 1, code, msg))) break;
                continue;
            }
            if (r.op != protocol::kIdentify) {
                if (c.proto == 0) {
                    if (!send(c, fail(r, 1, protocol::err::kIdentifyFirst, "IDENTIFY d'abord (la version du protocole)"))) break;
                    continue;
                }
                if (r.proto != c.proto) {
                    if (!send(c, fail(r, c.proto, protocol::err::kBadVersion,
                                      "version de protocole " + std::to_string(r.proto) + " : la version choisie est "
                                          + std::to_string(c.proto))))
                        break;
                    continue;
                }
                // Une operation deja faite : la meme reponse, sans rien refaire.
                Frame cached;
                if (cache.find(c.peerHex, r.id, cached)) {
                    if (cached.header.str("op") != r.op) {
                        if (!send(c, fail(r, c.proto, protocol::err::kReplay, "identifiant d'op\xC3\xA9ration d\xC3\xA9j\xC3\xA0 utilis\xC3\xA9 pour une autre op\xC3\xA9ration")))
                            break;
                        continue;
                    }
                    cached.header.set("seq", r.seq);
                    cached.header.set("replay", true);
                    if (!send(c, {cached.header, cached.data})) break;
                    continue;
                }
            }
            core::json::Value args;
            if (!f.data.empty() && (!core::json::parse(f.data, args, &msg) || !args.isObject())) {
                if (!send(c, fail(r, c.proto ? c.proto : 1, protocol::err::kBadArgs, "donn\xC3\xA9" "es illisibles (" + msg + ")"))) break;
                continue;
            }
            if (args.isNull()) args = core::json::Value::object();
            const Response resp = r.op == protocol::kIdentify ? identify(c, r, args) : dispatch(c, r, args);
            if (r.op != protocol::kIdentify) cache.put(c.peerHex, r.id, Frame{resp.header, resp.data});
            if (!send(c, resp)) break;
        }
        if (c.pairPending) pairFailed();   // PAIR_START sans PAIR_FINISH : un essai manque
        c.tls.close();
    }

    void reap(bool all) {
        std::vector<Worker> finished;
        {
            std::lock_guard<std::mutex> lock(workersMutex);
            for (auto it = workers.begin(); it != workers.end();) {
                if (all || it->done->load()) {
                    finished.push_back(std::move(*it));
                    it = workers.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& w : finished)
            if (w.thread.joinable()) w.thread.join();
    }

    void acceptLoop() {
        while (!stopping) {
            std::string peer;
            net::TcpSocket s = listener.accept(200, &peer);
            reap(false);
            if (!s.valid()) continue;
            if (active.load() >= cfg.maxConnections) {
                log("", "CONNEXION", "refus\xC3\xA9" "e : d\xC3\xA9j\xC3\xA0 " + std::to_string(cfg.maxConnections) + " connexions (" + peer + ")");
                s.close();
                continue;
            }
            auto done = std::make_shared<std::atomic<bool>>(false);
            ++active;
            std::thread t([this, done, peer](net::TcpSocket sock) {
                serve(std::move(sock), peer);
                --active;
                done->store(true);
            }, std::move(s));
            std::lock_guard<std::mutex> lock(workersMutex);
            workers.push_back({std::move(t), done});
        }
    }

    void udpLoop() {
        discovery::RateLimiter limiter;
        discovery::Announce a;
        a.machine = name;
        a.id = id;
        a.version = cfg.version;
        a.protos = protocol::acceptedVersions();
        a.port = tcpPort;
        a.fingerprint = identity.fingerprintHex();
        while (!stopping) {
            net::Datagram d;
            const int rc = udp.receive(d, 200);
            if (rc <= 0) continue;
            std::string nonce;
            if (!discovery::parseDiscover(d.data, nonce)) continue;
            if (!limiter.allow(d.from, nonce, Clock::now())) continue;
            (void)udp.sendTo(d.from, d.fromPort, discovery::hereMessage(nonce, a), nullptr);
        }
    }

    void stateLoop() {
        std::unique_lock<std::mutex> lock(stateMutex);
        while (!stopping) {
            stateCv.wait_for(lock, std::chrono::milliseconds(std::max(100, cfg.stateIntervalMs)), [this] { return stopping.load(); });
            if (stopping) break;
            writeState(true);
        }
    }
};

Agent::Agent(AgentConfig config) : d_(std::make_unique<Impl>(std::move(config))) {}

Agent::~Agent() { stop(); }

bool Agent::start(std::string* why) {
    auto& d = *d_;
    if (d.isRunning) return true;
    const auto failWith = [&](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
    if (d.cfg.dir.empty()) return failWith("pas de dossier pour l'agent");
    if (!crypto::init()) return failWith("le hasard du syst\xC3\xA8me est indisponible (libsodium)");
    std::error_code ec;
    fs::create_directories(P(d.cfg.dir), ec);
    if (!fs::is_directory(P(d.cfg.dir), ec)) return failWith("impossible de cr\xC3\xA9" "er le dossier " + d.cfg.dir);
    d.name = d.cfg.machineName.empty() ? net::hostName() : d.cfg.machineName;
    d.journal = std::make_unique<Journal>(d.cfg.dir);
    std::string w;
    if (!TlsIdentity::loadOrCreate(d.cfg.dir, "identite", true, "XPG-Agent " + d.name, d.identity, &w)) return failWith(w);
    d.id = d.identity.machineId();
    d.studios = std::make_unique<pairing::StudioStore>(d.cfg.dir);
    if (!d.studios->load(&w)) return failWith(w);
    // Les ports : tous deux libres, sinon aucun n'ecoute.
    if (!d.listener.listen(d.cfg.bindAddress, d.cfg.port, &w)) {
        d.journal->write("", "DEMARRAGE", "refus\xC3\xA9 : " + w);
        return failWith("l'agent n'\xC3\xA9" "coute pas : " + w + (d.cfg.port ? " (un autre agent tourne d\xC3\xA9j\xC3\xA0 ? sinon choisis un autre port avec --port)" : ""));
    }
    d.tcpPort = d.listener.localPort();
    d.udpPort = -1;
    if (d.cfg.discoveryPort >= 0) {
        if (!d.udp.bind(d.cfg.bindAddress, d.cfg.discoveryPort, &w)) {
            d.listener.close();
            d.journal->write("", "DEMARRAGE", "refus\xC3\xA9 : " + w);
            return failWith("l'agent n'\xC3\xA9" "coute pas : " + w + " (un autre agent tourne d\xC3\xA9j\xC3\xA0 ? sinon choisis un autre port avec --port-decouverte)");
        }
        d.udpPort = d.udp.localPort();
    }
    d.stopping = false;
    d.startedAtMs = protocol::nowMs();
    d.startedAt = Clock::now();
    d.isRunning = true;
    d.writeState(true);
    d.journal->write("", "DEMARRAGE", d.name + " " + d.id + " version " + d.cfg.version + " TCP " + std::to_string(d.tcpPort)
                                           + (d.udpPort >= 0 ? " UDP " + std::to_string(d.udpPort) : std::string(" sans d\xC3\xA9" "couverte"))
                                           + " empreinte " + crypto::fingerprintText(d.identity.fingerprint(), 0));
    d.acceptThread = std::thread([&d] { d.acceptLoop(); });
    if (d.udpPort >= 0) d.udpThread = std::thread([&d] { d.udpLoop(); });
    d.stateThread = std::thread([&d] { d.stateLoop(); });
    return true;
}

void Agent::stop() {
    auto& d = *d_;
    if (!d.isRunning) return;
    {
        std::lock_guard<std::mutex> lock(d.stateMutex);
        d.stopping = true;
    }
    d.stateCv.notify_all();
    if (d.acceptThread.joinable()) d.acceptThread.join();
    if (d.udpThread.joinable()) d.udpThread.join();
    if (d.stateThread.joinable()) d.stateThread.join();
    d.reap(true);
    d.listener.close();
    d.udp.close();
    d.isRunning = false;
    d.writeState(false);
    d.journal->write("", "ARRET", d.name);
}

bool Agent::running() const { return d_->isRunning; }
int Agent::port() const { return d_->tcpPort; }
int Agent::discoveryPort() const { return d_->udpPort; }
const TlsIdentity& Agent::identity() const { return d_->identity; }
std::string Agent::machineName() const { return d_->name; }
std::string Agent::machineId() const { return d_->id; }
int Agent::activeConnections() const { return d_->active.load(); }

// =================================================================== divers ===
std::string defaultAgentDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
    std::string base = "C:\\ProgramData";
    if (n > 0 && n < MAX_PATH) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            base.assign(static_cast<std::size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), base.data(), len, nullptr, nullptr);
        }
    }
    return base + "\\XPG\\Agent";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return std::string(x) + "/xpg-agent";
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share/xpg-agent";
    return ".xpg-agent";
#endif
}

std::string systemDescription() {
#if defined(_WIN32)
    const auto reg = [](const wchar_t* value) -> std::string {
        wchar_t buf[256];
        DWORD size = sizeof buf;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", value, RRF_RT_REG_SZ, nullptr, buf,
                         &size) != ERROR_SUCCESS)
            return {};
        const int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
        if (len <= 1) return {};
        std::string s(static_cast<std::size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, s.data(), len, nullptr, nullptr);
        return s;
    };
    std::string out = reg(L"ProductName");
    if (out.empty()) out = "Windows";
    const std::string build = reg(L"CurrentBuild");
    if (!build.empty()) out += " (build " + build + ")";
    return out;
#else
    std::ifstream f("/etc/os-release");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string v = line.substr(12);
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (!v.empty()) return v;
    }
    utsname u{};
    if (::uname(&u) == 0) return std::string(u.sysname) + " " + u.release;
    return "Linux";
#endif
}

bool readAgentState(const std::string& dir, core::json::Value& out, std::string* why) {
    crypto::Bytes raw;
    if (!crypto::readFile(joinPath(dir, "etat.json"), raw, why, 1024 * 1024)) return false;
    std::string err;
    if (!core::json::parse(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), out, &err) || !out.isObject()) {
        if (why) *why = joinPath(dir, "etat.json") + " illisible (" + err + ")";
        return false;
    }
    return true;
}

} // namespace deploy
