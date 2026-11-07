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
#  include <aclapi.h>
#  include <sddl.h>
#  include <shlobj.h>
#else
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

#include "Agent.hpp"

#include "Deployer.hpp"
#include "Discovery.hpp"
#include "Frame.hpp"
#include "Install.hpp"
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
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace deploy {

namespace {

namespace fs = std::filesystem;
fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

using Clock = std::chrono::steady_clock;

// 1.32.0 : IDENTIFY, les versions annoncees au plus (une liste sans fin remplissait le journal).
constexpr std::size_t kMaxProtos = 16;

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
    std::unique_ptr<Deployer> deployer;   // 1.29.0 : l'envoi, l'installation, le poste
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

    // 1.32.0 : les places par adresse, le rythme des PAIR_START, les refus notes sans remplir le journal.
    std::mutex slotsMutex;
    std::map<std::string, int> perAddress;
    std::mutex pairRateMutex;
    std::map<std::string, Clock::time_point> pairStarts;
    struct Quiet {
        Clock::time_point last{};
        long skipped{0};
        bool seen{false};
    };
    std::mutex quietMutex;
    std::map<std::string, Quiet> quiet;

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
        if (deployer) {
            v.set("station", deployer->stationState());
            v.set("apps", deployer->appsSummary());
            v.set("runtimeProgram", deployer->runtimeProgram());
        }
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
    // Version 2 : une reponse d'erreur peut porter des donnees (le detail).
    Response failData(const protocol::Request& r, int proto, std::string_view code, const std::string& message, const core::json::Value& data) {
        return {protocol::errorHeader(r.op, r.id, r.seq, code, message, proto), core::json::write(data, false)};
    }

    void log(const std::string& opId, std::string_view op, const std::string& detail) { journal->write(opId, op, detail); }

    std::string who(const Conn& c) const {
        return (c.studioName.empty() ? std::string("?") : Journal::field(c.studioName)) + " " + c.address + " " + shortFp(c.peer);
    }

    // 1.32.0 : un refus de connexion (ou une poignee de main manquee) : une ligne par motif toutes les 10 s au
    // plus, le nombre des autres dit dans la suivante - une rafale ne remplit pas le journal.
    void logQuiet(const std::string& reason, std::string_view op, const std::string& text) {
        std::string more;
        {
            std::lock_guard<std::mutex> lock(quietMutex);
            Quiet& q = quiet[reason];
            const auto now = Clock::now();
            if (q.seen && now - q.last < std::chrono::seconds(10)) {
                ++q.skipped;
                return;
            }
            if (q.skipped) more = " (et " + std::to_string(q.skipped) + " autre(s) depuis la ligne pr\xC3\xA9" "c\xC3\xA9" "dente)";
            q.seen = true;
            q.last = now;
            q.skipped = 0;
        }
        log("", op, text + more);
    }

    // 1.32.0 : maxPerAddress connexions a la fois par adresse.
    bool takeSlot(const std::string& address) {
        std::lock_guard<std::mutex> lock(slotsMutex);
        int& n = perAddress[address];
        if (n >= std::max(1, cfg.maxPerAddress)) return false;
        ++n;
        return true;
    }
    void releaseSlot(const std::string& address) {
        std::lock_guard<std::mutex> lock(slotsMutex);
        const auto it = perAddress.find(address);
        if (it != perAddress.end() && --it->second <= 0) perAddress.erase(it);
    }

    // 1.32.0 : un PAIR_START par adresse dans pairStartIntervalMs (deviner le code prend du temps).
    bool pairStartAllowed(const std::string& address) {
        if (cfg.pairStartIntervalMs <= 0) return true;
        const auto now = Clock::now();
        const auto gap = std::chrono::milliseconds(cfg.pairStartIntervalMs);
        std::lock_guard<std::mutex> lock(pairRateMutex);
        if (pairStarts.size() > 1024)
            for (auto it = pairStarts.begin(); it != pairStarts.end();) it = now - it->second >= gap ? pairStarts.erase(it) : std::next(it);
        const auto [it, fresh] = pairStarts.try_emplace(address, now);
        if (fresh) return true;
        if (now - it->second < gap) return false;
        it->second = now;
        return true;
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
        c.studioName = args.str("machine").substr(0, 128);
        const auto& offered = args.at("protos").items();
        if (offered.size() > kMaxProtos) {
            log(r.id, r.op, "refus\xC3\xA9 : " + std::to_string(offered.size()) + " versions annonc\xC3\xA9" "es (16 au plus) " + who(c));
            return fail(r, 1, protocol::err::kBadArgs,
                        "trop de versions de protocole annonc\xC3\xA9" "es (" + std::to_string(offered.size()) + ", 16 au plus)");
        }
        std::vector<int> theirs;
        for (const auto& p : offered)
            if (p.isNumber()) theirs.push_back(static_cast<int>(p.asInt()));
        if (theirs.empty()) theirs.push_back(r.proto);
        const int v = protocol::chooseVersion(protocol::acceptedVersions(), theirs);
        if (v == 0) {
            std::string list, mine;
            for (const int t : theirs) list += (list.empty() ? "" : ", ") + std::to_string(t);
            for (const int t : protocol::acceptedVersions()) mine += (mine.empty() ? "" : ", ") + std::to_string(t);
            log(r.id, r.op, "refus\xC3\xA9 : aucune version commune (Studio : " + Journal::field(list) + ") " + who(c));
            return fail(r, 1, protocol::err::kIncompatible,
                        "aucune version de protocole commune : l'agent (" + cfg.version + ") parle les versions " + mine
                            + ", IHM Studio la (les) version(s) " + list + " - mets \xC3\xA0 jour l'un des deux");
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
        if (!pairStartAllowed(c.address)) {
            log(r.id, r.op, "refus\xC3\xA9 : trop rapproch\xC3\xA9 (un essai par seconde au plus par adresse) " + who(c));
            return fail(r, c.proto, protocol::err::kBusy,
                        "un seul essai d'appairage par seconde depuis une m\xC3\xAAme adresse : r\xC3\xA9" "essaie dans un instant");
        }
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
        d.set("apps", deployer->appsSummary());   // 1.29.0 : les applications installees (nom, version active, en marche)
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

    // 1.29.0 : les commandes de la version 2 (deploy/Deployer).
    Response deployOp(Conn& c, const protocol::Request& r, const core::json::Value& args, std::string_view bytes) {
        if (c.proto < protocol::kDeployProto) {
            log(r.id, r.op, "refus\xC3\xA9 : version 1 du protocole " + who(c));
            return fail(r, c.proto, protocol::err::kBadVersion,
                        r.op + " demande la version " + std::to_string(protocol::kDeployProto)
                            + " du protocole (IHM Studio 1.29.0 ou plus r\xC3\xA9" "cent) ; cette connexion parle la version "
                            + std::to_string(c.proto) + " : mets \xC3\xA0 jour IHM Studio pour d\xC3\xA9ployer sur ce PC");
        }
        Peer peer;
        peer.fingerprint = c.peerHex;
        peer.address = c.address;
        pairing::PairedStudio ps;
        peer.name = studios->find(c.peerHex, ps) ? ps.name : c.studioName;
        OpResult res;
        if (r.op == protocol::kPrepare) res = deployer->prepare(peer, args);
        else if (r.op == protocol::kSendChunk) res = deployer->sendChunk(peer, args, bytes);
        else if (r.op == protocol::kVerifyPackage) res = deployer->verify(peer, args);
        else if (r.op == protocol::kInstall) res = deployer->install(peer, args);
        else if (r.op == protocol::kStop) res = deployer->stop(peer, args);
        else if (r.op == protocol::kStart) res = deployer->startStation(peer, args);
        else if (r.op == protocol::kRollback) res = deployer->rollback(peer, args);
        else if (r.op == protocol::kListApps) res = deployer->listApps(peer, args);
        else res = deployer->history(peer, args);
        // Un bloc ne fait pas une ligne du journal : seulement le dernier, ou un refus.
        if (r.op != protocol::kSendChunk || !res.ok || res.data.flag("complete"))
            log(r.id, r.op, (res.ok ? "ok " : "refus\xC3\xA9 : ") + res.journal + " " + who(c));
        if (!res.ok) return failData(r, c.proto, res.code, res.message, res.data);
        return ok(r, c.proto, res.data);
    }

    Response dispatch(Conn& c, const protocol::Request& r, const core::json::Value& args, std::string_view bytes) {
        if (r.op == protocol::kPairStart) return pairStart(c, r, args);
        if (r.op == protocol::kPairFinish) return pairFinish(c, r, args);
        const bool known = r.op == protocol::kStatus || r.op == protocol::kGetLogs || r.op == protocol::kUnpair || protocol::isDeployOp(r.op);
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
        if (r.op == protocol::kUnpair) return unpair(c, r);
        return deployOp(c, r, args, bytes);
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
            if (!stopping) logQuiet("tls", "TLS", "refus\xC3\xA9 " + peer + " : " + why);
            return;
        }
        c.peer = c.tls.peerFingerprint();
        c.peerHex = crypto::hex(c.peer);
        c.address = c.tls.peerAddress();
        // 1.32.0 : les dernieres places sont gardees aux PC appaires (un pair inconnu est refuse s'il en reste
        // moins de pairedReserve + 1 en le comptant).
        if (!studios->contains(c.peerHex) && active.load() > cfg.maxConnections - cfg.pairedReserve) {
            logQuiet("reserve", "CONNEXION",
                     "refus\xC3\xA9" "e : PC non appair\xC3\xA9, les " + std::to_string(cfg.pairedReserve) + " derni\xC3\xA8res places sont gard\xC3\xA9" "es aux PC appair\xC3\xA9s ("
                         + who(c) + ")");
            c.tls.close();
            return;
        }
        for (;;) {
            Frame f;
            std::uint32_t announced = 0;
            // 1.32.0 : un pair inconnu muet ne garde pas longtemps sa place.
            const int idleMs = studios->contains(c.peerHex) ? cfg.idleTimeoutMs : cfg.unpairedIdleTimeoutMs;
            const FrameRead rd = readFrame(c.tls, f, idleMs, cfg.ioTimeoutMs, &why, &announced);
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
            std::string_view bytes;
            if (r.op == protocol::kSendChunk) {
                // SEND_CHUNK : les arguments dans l'en-tete ("chunk"), les octets du bloc dans les donnees.
                args = f.header.at("chunk");
                bytes = f.data;
            } else if (!f.data.empty() && (!core::json::parse(f.data, args, &msg) || !args.isObject())) {
                if (!send(c, fail(r, c.proto ? c.proto : 1, protocol::err::kBadArgs, "donn\xC3\xA9" "es illisibles (" + msg + ")"))) break;
                continue;
            }
            if (!args.isObject()) args = core::json::Value::object();
            const bool paired = studios->contains(c.peerHex);   // avant la demande (PAIR_FINISH ne compte pas)
            const Response resp = r.op == protocol::kIdentify ? identify(c, r, args) : dispatch(c, r, args, bytes);
            // 1.32.0 : seules les reponses a un PC appaire sont gardees (un pair inconnu ne remplit pas le cache).
            if (r.op != protocol::kIdentify && paired) cache.put(c.peerHex, r.id, Frame{resp.header, resp.data});
            if (!send(c, resp)) break;
            // 1.32.0 : un PAIR_FINISH manque ferme la connexion (un autre essai : une autre connexion).
            if (r.op == protocol::kPairFinish && !resp.header.flag("ok")) break;
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
                logQuiet("plein", "CONNEXION", "refus\xC3\xA9" "e : d\xC3\xA9j\xC3\xA0 " + std::to_string(cfg.maxConnections) + " connexions (" + peer + ")");
                s.close();
                continue;
            }
            const std::string address = s.peerAddress();
            if (!takeSlot(address)) {
                logQuiet("adresse", "CONNEXION",
                         "refus\xC3\xA9" "e : d\xC3\xA9j\xC3\xA0 " + std::to_string(cfg.maxPerAddress) + " connexions depuis cette adresse (" + peer + ")");
                s.close();
                continue;
            }
            auto done = std::make_shared<std::atomic<bool>>(false);
            ++active;
            std::thread t([this, done, peer, address](net::TcpSocket sock) {
                serve(std::move(sock), peer);
                releaseSlot(address);
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
    // 1.32.0 : le dossier ferme aux autres comptes du PC, a chaque demarrage (un echec est dit, l'agent demarre).
    if (!restrictAgentDir(d.cfg.dir, &w)) d.journal->write("", "DOSSIER", "attention : " + w);
    // 1.32.0 : la cle protegee pour l'utilisateur (la machine : tout compte du PC pouvait la relire).
    if (!TlsIdentity::loadOrCreate(d.cfg.dir, "identite", false, "XPG-Agent " + d.name, d.identity, &w)) return failWith(w);
    d.id = d.identity.machineId();
    d.studios = std::make_unique<pairing::StudioStore>(d.cfg.dir);
    if (!d.studios->load(&w)) return failWith(w);
    DeployerConfig dc;
    dc.dir = d.cfg.dir;
    dc.runtimeProgram = d.cfg.runtimeProgram;
    dc.keepVersions = d.cfg.keepVersions;
    dc.startCheckMs = d.cfg.startCheckMs;
    dc.stopGraceMs = d.cfg.stopGraceMs;
    dc.simulatedFreeBytes = d.cfg.simulatedFreeBytes;
    d.deployer = std::make_unique<Deployer>(dc, *d.studios, *d.journal, d.stopping);
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
    d.deployer->start();   // les installations coupees, le poste lance avant un redemarrage
    d.startedAtMs = protocol::nowMs();
    d.startedAt = Clock::now();
    d.isRunning = true;
    d.writeState(true);
    d.journal->write("", "DEMARRAGE", d.name + " " + d.id + " version " + d.cfg.version + " TCP " + std::to_string(d.tcpPort)
                                           + (d.udpPort >= 0 ? " UDP " + std::to_string(d.udpPort) : std::string(" sans d\xC3\xA9" "couverte"))
                                           + " empreinte " + crypto::fingerprintText(d.identity.fingerprint(), 0)
                                           + " poste " + d.deployer->runtimeProgram()
                                           + (d.cfg.simulatedFreeBytes >= 0 ? " (ESSAI : place libre simul\xC3\xA9" "e " + std::to_string(d.cfg.simulatedFreeBytes) + " octets)" : std::string()));
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
std::string Agent::runtimeProgram() const { return d_->deployer ? d_->deployer->runtimeProgram() : std::string(); }

// ================================================================== le dossier ===
#if defined(_WIN32)
namespace {

std::string utf8Of(const wchar_t* w) {
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), len, nullptr, nullptr);
    return s;
}

std::string envText(const wchar_t* name) {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? utf8Of(buf) : std::string();
}

// Le SID du compte de ce processus (il vit dans `buf`) ; nullptr : illisible.
PSID tokenUser(std::vector<unsigned char>& buf) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
    DWORD len = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &len);
    PSID sid = nullptr;
    if (len > 0) {
        buf.assign(len, 0);
        if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) sid = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;
    }
    CloseHandle(token);
    return sid;
}

// Le proprietaire de `p` est-il ce compte, les Administrateurs ou le systeme ? Illisible : non.
bool trustedOwner(const fs::path& p, PSID me) {
    std::wstring w = p.wstring();
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(w.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS)
        return false;
    const bool ok = owner && (EqualSid(owner, me) || IsWellKnownSid(owner, WinBuiltinAdministratorsSid) || IsWellKnownSid(owner, WinLocalSystemSid));
    LocalFree(sd);
    return ok;
}

// Le DACL de `path` est-il deja celui voulu (protege, les memes entrees) ? Alors rien n'est refait :
// SetNamedSecurityInfo repasse sur tout le contenu (les versions installees : des milliers de fichiers).
bool sameDacl(std::wstring& path, PACL want) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PACL have = nullptr;
    if (GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &have, nullptr, &sd) != ERROR_SUCCESS)
        return false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool same = have && want && GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED) != 0
                && have->AceCount == want->AceCount;
    constexpr BYTE kFlags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | NO_PROPAGATE_INHERIT_ACE | INHERIT_ONLY_ACE | INHERITED_ACE;
    for (DWORD i = 0; same && i < want->AceCount; ++i) {
        void* a = nullptr;
        void* b = nullptr;
        if (!GetAce(have, i, &a) || !GetAce(want, i, &b)) {
            same = false;
            break;
        }
        auto* x = static_cast<ACCESS_ALLOWED_ACE*>(a);
        auto* y = static_cast<ACCESS_ALLOWED_ACE*>(b);
        same = x->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && y->Header.AceType == ACCESS_ALLOWED_ACE_TYPE
               && (x->Header.AceFlags & kFlags) == (y->Header.AceFlags & kFlags) && x->Mask == y->Mask && EqualSid(&x->SidStart, &y->SidStart);
    }
    LocalFree(sd);
    return same;
}

// Dans le dossier repris : ce qui appartient a un autre compte, et les liens, est efface (sans les suivre).
void dropForeign(const fs::path& dir, PSID me, int depth, std::vector<std::string>& dropped, std::vector<std::string>& stuck) {
    std::vector<fs::path> children;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) children.push_back(it->path());
    for (const auto& c : children) {
        const auto u8 = c.u8string();
        const std::string u(u8.begin(), u8.end());
        if (install::isLink(u) || !trustedOwner(c, me)) {
            (install::removeTree(u) ? dropped : stuck).push_back(u);
            continue;
        }
        std::error_code e2;
        if (depth < 64 && fs::is_directory(c, e2)) dropForeign(c, me, depth + 1, dropped, stuck);
    }
}

// La copie (l'ancien dossier sur un autre volume) : ce qui appartient a un autre compte, et les liens, ne
// sont pas copies (`skipped`).
bool copyTrusted(const fs::path& from, const fs::path& to, PSID me, int depth, int& skipped, std::string& why) {
    std::error_code ec;
    if (!fs::create_directory(to, ec) || ec) {
        why = "impossible de cr\xC3\xA9" "er le dossier (" + (ec ? ec.message() : std::string("il existe d\xC3\xA9j\xC3\xA0")) + ")";
        return false;
    }
    std::vector<fs::path> children;
    for (fs::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) children.push_back(it->path());
    if (ec) {
        why = "lecture de l'ancien dossier : " + ec.message();
        return false;
    }
    for (const auto& c : children) {
        const auto u8 = c.u8string();
        const std::string u(u8.begin(), u8.end());
        if (install::isLink(u) || !trustedOwner(c, me)) {
            ++skipped;
            continue;
        }
        std::error_code e2;
        if (fs::is_directory(c, e2)) {
            if (depth >= 64 || !copyTrusted(c, to / c.filename(), me, depth + 1, skipped, why)) return false;
        } else if (!fs::copy_file(c, to / c.filename(), fs::copy_options::none, e2) || e2) {
            why = "copie de " + u + " : " + e2.message();
            return false;
        }
    }
    return true;
}

// Les secrets proteges pour la machine (avant 1.32.0) : relus (DPAPI relit les deux), reproteges pour ce compte.
void reprotect(const std::string& dir, std::vector<std::string>& out) {
    for (const char* name : {"identite.cle", "appairage.code"}) {
        const std::string path = joinPath(dir, name);
        std::error_code ec;
        if (!fs::exists(P(path), ec)) continue;
        crypto::Bytes sealed, plain, again;
        std::string w;
        const bool ok = crypto::readFile(path, sealed, &w, 64 * 1024) && crypto::unprotect(sealed, plain, &w) && crypto::protect(plain, again, false, &w)
                        && crypto::writePrivateFile(path, again, &w);
        if (!plain.empty()) crypto::wipe(plain.data(), plain.size());
        if (!ok) out.push_back("ATTENTION : " + path + " n'a pas pu \xC3\xAAtre reprot\xC3\xA9g\xC3\xA9 pour ce compte (" + w + ").");
    }
}

std::string listed(const std::vector<std::string>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size() && i < 3; ++i) s += (i ? ", " : "") + items[i];
    if (items.size() > 3) s += "...";
    return s;
}

} // namespace
#endif

std::string defaultAgentDir() {
#if defined(_WIN32)
    // 1.32.0 : le profil de l'operateur (l'agent tourne dans sa session) : les autres comptes du PC n'y ecrivent pas.
    wchar_t buf[MAX_PATH];
    std::string base;
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf))) base = utf8Of(buf);
    if (base.empty()) base = envText(L"LOCALAPPDATA");
    if (base.empty()) return ".xpg-agent";
    return base + "\\XPG\\Agent";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return std::string(x) + "/xpg-agent";
    if (const char* h = std::getenv("HOME"); h && *h) return std::string(h) + "/.local/share/xpg-agent";
    return ".xpg-agent";
#endif
}

std::string legacyAgentDir() {
#if defined(_WIN32)
    std::string base = envText(L"ProgramData");
    if (base.empty()) base = "C:\\ProgramData";
    return base + "\\XPG\\Agent";
#else
    return {};
#endif
}

bool restrictAgentDir(const std::string& dir, std::string* why) {
    const auto failWith = [why](std::string text) {
        if (why) *why = std::move(text);
        return false;
    };
#if defined(_WIN32)
    std::vector<unsigned char> buf;
    const PSID me = tokenUser(buf);
    if (!me) return failWith("le compte de ce processus est illisible (erreur " + std::to_string(GetLastError()) + ") : le dossier " + dir + " n'est pas ferm\xC3\xA9");
    LPWSTR meText = nullptr;
    if (!ConvertSidToStringSidW(me, &meText)) return failWith("SID illisible (erreur " + std::to_string(GetLastError()) + ") : le dossier " + dir + " n'est pas ferm\xC3\xA9");
    // P : sans heritage du parent ; OICI : les fichiers et dossiers dessous en heritent ; FA : controle total.
    const std::wstring sddl = std::wstring(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;") + meText + L")";
    LocalFree(meText);
    PSECURITY_DESCRIPTOR wanted = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &wanted, nullptr))
        return failWith("DACL impossible \xC3\xA0 construire (erreur " + std::to_string(GetLastError()) + ") : le dossier " + dir + " n'est pas ferm\xC3\xA9");
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    std::wstring path = P(dir).wstring();
    DWORD rc = ERROR_SUCCESS;
    if (!GetSecurityDescriptorDacl(wanted, &present, &dacl, &defaulted) || !present || !dacl) rc = ERROR_INVALID_ACL;
    else if (!sameDacl(path, dacl))
        rc = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, dacl, nullptr);
    LocalFree(wanted);
    if (rc != ERROR_SUCCESS)
        return failWith("le dossier " + dir + " n'a pas pu \xC3\xAAtre ferm\xC3\xA9 aux autres comptes (SYSTEM, Administrateurs, ce compte : erreur "
                        + std::to_string(rc) + ")");
    return true;
#else
    std::error_code ec;
    fs::permissions(P(dir), fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) return failWith("le dossier " + dir + " n'a pas pu \xC3\xAAtre mis en 0700 (" + ec.message() + ")");
    return true;
#endif
}

std::vector<std::string> migrateLegacyAgentDir(const std::string& newDir) {
    std::vector<std::string> out;
#if defined(_WIN32)
    const std::string old = legacyAgentDir();
    if (old.empty() || newDir.empty()) return out;
    std::error_code ec;
    const fs::path from = P(old), to = P(newDir);
    if (fs::exists(to, ec) || install::isLink(newDir)) return out;   // deja repris (ou deja neuf) : une fois seulement
    const std::string fresh = " L'agent repart d'un dossier neuf (" + newDir
                              + ") : r\xC3\xA9" "appaire les PC d'IHM Studio (--appairer) et red\xC3\xA9ploie. Fais v\xC3\xA9rifier puis effacer " + old
                              + " par un administrateur.";
    if (install::isLink(old)) {
        out.push_back("ATTENTION : l'ancien dossier de l'agent " + old + " est un lien (ou une jonction) : il n'est PAS repris." + fresh);
        return out;
    }
    ec.clear();
    if (!fs::is_directory(from, ec)) return out;   // rien a reprendre
    std::vector<unsigned char> buf;
    const PSID me = tokenUser(buf);
    if (!me || !trustedOwner(from, me)) {
        out.push_back("ATTENTION : l'ancien dossier de l'agent " + old
                      + " n'appartient ni \xC3\xA0 ce compte, ni aux Administrateurs, ni au syst\xC3\xA8me : il n'est PAS repris (un autre compte de ce PC a pu le pr\xC3\xA9parer)."
                      + fresh);
        return out;
    }
    fs::create_directories(to.parent_path(), ec);
    // Le meme volume : un seul renommage (les droits et les dates suivent). Sinon : une copie.
    const bool moved = MoveFileExW(from.c_str(), to.c_str(), 0) != 0;
    if (!moved) {
        const DWORD moveError = GetLastError();
        const std::string tmpText = newDir + ".migration";
        int skipped = 0;
        std::string w;
        const bool copied = install::removeTree(tmpText, &w) && copyTrusted(from, P(tmpText), me, 0, skipped, w)
                            && MoveFileExW(P(tmpText).c_str(), to.c_str(), 0) != 0;
        if (!copied) {
            (void)install::removeTree(tmpText);
            out.push_back("ATTENTION : l'ancien dossier de l'agent " + old + " ne se d\xC3\xA9place pas (erreur " + std::to_string(moveError)
                          + ") et sa copie a \xC3\xA9" "chou\xC3\xA9 (" + (w.empty() ? "erreur " + std::to_string(GetLastError()) : w) + "). L'agent repart d'un dossier neuf ("
                          + newDir + ") ; pour garder les appairages et les versions install\xC3\xA9" "es, arr\xC3\xAAte l'agent, efface " + newDir + " et recopie "
                          + old + " \xC3\xA0 sa place.");
            return out;
        }
        if (skipped)
            out.push_back("ATTENTION : " + std::to_string(skipped) + " \xC3\xA9l\xC3\xA9ment(s) de l'ancien dossier (\xC3\xA0 un autre compte de ce PC, ou des liens) n'ont pas \xC3\xA9t\xC3\xA9 recopi\xC3\xA9s.");
        const std::string note = "Ce dossier n'est plus utilis\xC3\xA9 par XPG-Deployment-Agent : il a \xC3\xA9t\xC3\xA9 recopi\xC3\xA9 vers\r\n" + newDir
                                 + "\r\n(le dossier de l'op\xC3\xA9rateur, ferm\xC3\xA9 aux autres comptes du PC). Un administrateur peut l'effacer.\r\n";
        (void)crypto::replaceFile(joinPath(old, "DOSSIER-DEPLACE.txt"), note, false, nullptr);
    }
    std::string w;
    if (!restrictAgentDir(newDir, &w)) out.push_back("ATTENTION : " + w);
    std::vector<std::string> dropped, stuck;
    dropForeign(to, me, 0, dropped, stuck);
    if (!dropped.empty())
        out.push_back("ATTENTION : " + std::to_string(dropped.size()) + " \xC3\xA9l\xC3\xA9ment(s) repris appartenaient \xC3\xA0 un autre compte de ce PC (ou \xC3\xA9taient des liens) : effac\xC3\xA9s ("
                      + listed(dropped) + "). V\xC3\xA9rifie les PC appair\xC3\xA9s (--etat).");
    if (!stuck.empty())
        out.push_back("ATTENTION : " + std::to_string(stuck.size()) + " \xC3\xA9l\xC3\xA9ment(s) \xC3\xA0 un autre compte de ce PC ne s'effacent pas (" + listed(stuck)
                      + ") : fais-les effacer par un administrateur.");
    reprotect(newDir, out);
    out.insert(out.begin(), "Le dossier de l'agent a \xC3\xA9t\xC3\xA9 " + std::string(moved ? "d\xC3\xA9plac\xC3\xA9" : "recopi\xC3\xA9") + " de " + old + " vers " + newDir
                                + " : il est d\xC3\xA9sormais dans le profil de ce compte, ferm\xC3\xA9 aux autres comptes du PC"
                                + (moved ? std::string(".") : std::string(" (l'ancien reste, une note y dit o\xC3\xB9 est le nouveau).")));
#else
    (void)newDir;
#endif
    return out;
}

// =================================================================== divers ===

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
