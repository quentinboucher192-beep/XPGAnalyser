// =============================================================================
//  session/SessionLink.cpp - 1.43.0 : voir SessionLink.hpp
// =============================================================================
#include "SessionLink.hpp"

#include "../deploy/Crypto.hpp"
#include "../deploy/Discovery.hpp"
#include "../deploy/Frame.hpp"
#include "../deploy/Net.hpp"
#include "../deploy/Pairing.hpp"
#include "../deploy/Tls.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace session {

namespace {

using Clock = std::chrono::steady_clock;
using deploy::Frame;
using deploy::FrameRead;

constexpr int kReadSliceMs = 30;          // une lecture rend la main en 30 ms au plus
constexpr int kFrameMs = 20000;           // le reste d'une trame commencee
constexpr int kPingAfterMs = 5000;        // rien envoye depuis 5 s : PING
constexpr int kSilenceMs = 30000;         // rien recu depuis 30 s : la connexion est perdue
constexpr int kHandshakeMs = 10000;

core::json::Value op(std::string_view name) {
    core::json::Value v = core::json::Value::object();
    v.set("op", std::string(name));
    return v;
}

// Une connexion et sa file d'envoi.
struct Conn {
    int id{0};
    deploy::TlsStream tls;
    std::string address, fingerprint;
    std::mutex m;
    std::deque<std::pair<core::json::Value, std::string>> out;
    std::size_t outBytes{0};
    bool closeWhenFlushed{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    std::thread th;

    void push(core::json::Value h, std::string data, bool thenClose = false) {
        std::lock_guard<std::mutex> lock(m);
        if (closeWhenFlushed) return;
        outBytes += data.size() + 256;
        out.emplace_back(std::move(h), std::move(data));
        if (thenClose) closeWhenFlushed = true;
    }
};

// Lire ET ecrire, jusqu'a la fin de la connexion : rend pourquoi elle s'arrete ("" : fermee par nous).
std::string pump(Conn& c, const std::function<void(Frame&&)>& onFrame) {
    auto lastRecv = Clock::now(), lastSend = Clock::now();
    for (;;) {
        if (c.stop) return "arr\xC3\xAAt";
        for (;;) {
            std::pair<core::json::Value, std::string> next;
            {
                std::lock_guard<std::mutex> lock(c.m);
                if (c.out.empty()) break;
                next = std::move(c.out.front());
                c.out.pop_front();
                c.outBytes -= std::min(c.outBytes, next.second.size() + 256);
            }
            std::string why;
            if (!deploy::writeFrame(c.tls, next.first, next.second, kFrameMs, &why)) return "envoi impossible : " + why;
            lastSend = Clock::now();
        }
        {
            std::lock_guard<std::mutex> lock(c.m);
            if (c.closeWhenFlushed && c.out.empty()) return {};
        }
        if (Clock::now() - lastSend > std::chrono::milliseconds(kPingAfterMs)) {
            std::string why;
            if (!deploy::writeFrame(c.tls, op("PING"), {}, kFrameMs, &why)) return "envoi impossible : " + why;
            lastSend = Clock::now();
        }
        Frame f;
        std::string why;
        switch (deploy::readFrame(c.tls, f, kReadSliceMs, kFrameMs, &why)) {
            case FrameRead::Ok:
                lastRecv = Clock::now();
                if (f.header.str("op") == "PING") break;
                onFrame(std::move(f));
                break;
            case FrameRead::Idle:
                if (Clock::now() - lastRecv > std::chrono::milliseconds(kSilenceMs)) return "plus de nouvelles depuis 30 s";
                break;
            case FrameRead::Closed: return "l'autre PC a ferm\xC3\xA9 la connexion";
            case FrameRead::Cancelled: return "arr\xC3\xAAt";
            case FrameRead::TooBig: return "trame trop grosse";
            default: return why.empty() ? std::string("connexion perdue") : why;
        }
    }
}

// Lire une trame de la poignee de main (une seule attendue a la fois).
bool readOne(Conn& c, Frame& f, int ms, std::string* why) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        if (c.stop) { if (why) *why = "arr\xC3\xAAt"; return false; }
        const auto r = deploy::readFrame(c.tls, f, 100, kFrameMs, why);
        if (r == FrameRead::Ok) {
            if (f.header.str("op") == "PING") continue;
            return true;
        }
        if (r != FrameRead::Idle) {
            if (why && why->empty()) *why = "connexion perdue";
            return false;
        }
        if (Clock::now() >= deadline) { if (why) *why = "pas de r\xC3\xA9ponse"; return false; }
    }
}

bool sendOne(Conn& c, const core::json::Value& h, std::string* why) { return deploy::writeFrame(c.tls, h, {}, kFrameMs, why); }

} // namespace

std::string sessionBinding(const std::string& sessionId) { return deploy::crypto::sha256Hex("XPG-SESSION-v1|" + sessionId); }

// =============================================================================================== l'hote ===
struct HostLink::Impl {
    HostConfig cfg;
    deploy::TlsIdentity identity;
    deploy::net::TcpSocket listener;
    deploy::net::UdpSocket udp;
    int tcpPort{-1}, udpPort{-1};
    std::atomic<bool> stopping{false};
    std::thread acceptThread, udpThread;

    std::mutex announceMutex;
    Announce announce;

    std::mutex connsMutex;
    std::map<int, std::shared_ptr<Conn>> conns;
    int nextConn{1};

    std::mutex eventsMutex;
    std::vector<LinkEvent> events;

    std::mutex attemptsMutex;
    std::map<std::string, int> attempts;   // par empreinte : les codes faux

    void post(LinkEvent e) {
        std::lock_guard<std::mutex> lock(eventsMutex);
        events.push_back(std::move(e));
    }

    std::shared_ptr<Conn> find(int id) {
        std::lock_guard<std::mutex> lock(connsMutex);
        const auto it = conns.find(id);
        return it == conns.end() ? nullptr : it->second;
    }

    void refuse(Conn& c, const std::string& reason) {
        core::json::Value h = op("REFUS");
        h.set("raison", reason);
        std::string why;
        (void)sendOne(c, h, &why);
    }

    // La poignee de main, puis les trames : dans le fil de la connexion.
    void serve(const std::shared_ptr<Conn>& c, deploy::net::TcpSocket sock) {
        std::string why;
        LinkEvent closed;
        closed.kind = LinkEvent::Kind::Closed;
        closed.conn = c->id;
        const auto end = [&](std::string reason) {
            closed.why = std::move(reason);
            closed.fingerprint = c->fingerprint;
            closed.address = c->address;
            c->tls.close();
            c->done = true;
            post(std::move(closed));
        };
        c->tls.setCancel(&c->stop);
        if (!c->tls.accept(std::move(sock), identity, kHandshakeMs, &why)) { end("poign\xC3\xA9" "e de main TLS : " + why); return; }
        c->fingerprint = c->tls.peerFingerprintHex();
        c->address = c->tls.peerAddress();
        Frame hello;
        if (!readOne(*c, hello, kHandshakeMs, &why)) { end("BONJOUR attendu : " + why); return; }
        if (hello.header.str("op") != "BONJOUR" || hello.header.integer("proto") < 1) {
            refuse(*c, "ce n'est pas une IHM Studio qui rejoint une session");
            end("BONJOUR attendu");
            return;
        }
        if (hello.header.str("sid") != cfg.sessionId) {
            refuse(*c, "la session a chang\xC3\xA9 (elle a red\xC3\xA9marr\xC3\xA9) : cherche-la de nouveau");
            end("une autre session");
            return;
        }
        bool codeOk = false;
        if (!cfg.code.empty()) {
            {
                std::lock_guard<std::mutex> lock(attemptsMutex);
                if (attempts[c->fingerprint] >= kCodeAttempts) {
                    refuse(*c, "trop de codes faux depuis ce PC : demande \xC3\xA0 l'h\xC3\xB4te de t'accepter");
                    end("trop de codes faux");
                    return;
                }
            }
            if (!sendOne(*c, op("CODE_DEMANDE"), &why)) { end(why); return; }
            Frame start;
            if (!readOne(*c, start, 120000, &why) || start.header.str("op") != "CODE_DEBUT") { end("code attendu : " + why); return; }
            deploy::pairing::AgentPairing ap(cfg.code, c->tls.peerFingerprint(), identity.fingerprint(), cfg.hostName, cfg.sessionId);
            core::json::Value reply;
            if (!ap.start(start.header.at("cpace"), reply, &why)) {
                refuse(*c, "code : " + why);
                end(why);
                return;
            }
            // L'essai compte des que l'hote repond : un equipier qui voit le code faux (la confirmation de l'hote ne
            // passe pas) s'arrete la, sans CODE_FIN. Un essai reussi ne compte pas.
            {
                std::lock_guard<std::mutex> lock(attemptsMutex);
                ++attempts[c->fingerprint];
            }
            core::json::Value h = op("CODE_REPONSE");
            h.set("cpace", std::move(reply));
            if (!sendOne(*c, h, &why)) { end(why); return; }
            Frame fin;
            if (!readOne(*c, fin, kHandshakeMs, &why) || fin.header.str("op") != "CODE_FIN") { end("code : " + why); return; }
            const bool good = ap.finish(fin.header.at("cpace"), &why) && ap.studioKeyHex() == sessionBinding(cfg.sessionId);
            if (good) {
                std::lock_guard<std::mutex> lock(attemptsMutex);
                --attempts[c->fingerprint];
            } else {
                int left;
                {
                    std::lock_guard<std::mutex> lock(attemptsMutex);
                    left = kCodeAttempts - attempts[c->fingerprint];
                }
                core::json::Value r = op("REFUS");
                r.set("raison", left > 0 ? "code faux (encore " + std::to_string(left) + " essai" + (left > 1 ? "s" : "") + ")"
                                         : std::string("code faux : plus d'essai depuis ce PC"));
                r.set("code_faux", true);
                r.set("essais", left);
                (void)sendOne(*c, r, &why);
                end("code faux");
                return;
            }
            codeOk = true;
        }
        // Le lien est fait (le code prouve s'il le fallait) : l'equipier attend maintenant la decision de l'hote.
        {
            core::json::Value ok = op("LIEN_OK");
            ok.set("code", codeOk);
            if (!sendOne(*c, ok, &why)) { end(why); return; }
        }
        LinkEvent knock;
        knock.kind = LinkEvent::Kind::Knock;
        knock.conn = c->id;
        knock.header = std::move(hello.header);
        knock.fingerprint = c->fingerprint;
        knock.address = c->address;
        knock.codeOk = codeOk;
        post(std::move(knock));
        const std::string reason = pump(*c, [&](Frame&& f) {
            LinkEvent e;
            e.kind = LinkEvent::Kind::Frame;
            e.conn = c->id;
            e.header = std::move(f.header);
            e.data = std::move(f.data);
            post(std::move(e));
        });
        end(reason);
    }

    void acceptLoop() {
        while (!stopping) {
            std::string peer;
            auto s = listener.accept(200, &peer);
            if (!s.valid()) continue;
            // Le menage : les connexions finies.
            {
                std::lock_guard<std::mutex> lock(connsMutex);
                for (auto it = conns.begin(); it != conns.end();) {
                    if (it->second->done && it->second->th.joinable()) {
                        it->second->th.join();
                        it = conns.erase(it);
                    } else {
                        ++it;
                    }
                }
                if (conns.size() >= 32) continue;   // une frappe en rafale : on ne suit pas
            }
            auto c = std::make_shared<Conn>();
            {
                std::lock_guard<std::mutex> lock(connsMutex);
                c->id = nextConn++;
                conns[c->id] = c;
            }
            c->th = std::thread([this, c, sock = std::move(s)]() mutable { serve(c, std::move(sock)); });
        }
    }

    void udpLoop() {
        deploy::discovery::RateLimiter limiter;
        while (!stopping) {
            deploy::net::Datagram d;
            const int rc = udp.receive(d, 200);
            if (rc <= 0) continue;
            std::string nonce;
            if (!parseSearch(d.data, nonce)) continue;
            if (!limiter.allow(d.from, nonce, Clock::now())) continue;
            Announce a;
            {
                std::lock_guard<std::mutex> lock(announceMutex);
                a = announce;
            }
            (void)udp.sendTo(d.from, d.fromPort, hereMessage(nonce, a), nullptr);
        }
    }
};

HostLink::HostLink() : d_(std::make_unique<Impl>()) {}
HostLink::~HostLink() { stop(); }

bool HostLink::start(const HostConfig& cfg, const deploy::TlsIdentity& identity, std::string* why) {
    stop();
    d_ = std::make_unique<Impl>();
    auto& d = *d_;
    d.cfg = cfg;
    d.identity = identity;
    if (!deploy::tls::ready(why)) return false;
    if (!deploy::net::startup()) {
        if (why) *why = "le r\xC3\xA9seau est indisponible";
        return false;
    }
    std::string err;
    const int span = cfg.port == 0 ? 1 : kPortSpan;
    for (int i = 0; i < span; ++i) {
        if (d.listener.listen(cfg.bindAddress, cfg.port == 0 ? 0 : cfg.port + i, &err)) {
            d.tcpPort = d.listener.localPort();
            break;
        }
    }
    if (d.tcpPort < 0) {
        if (why) *why = "aucun port libre pour la session (" + std::to_string(cfg.port) + " \xC3\xA0 " + std::to_string(cfg.port + span - 1) + ") : " + err;
        return false;
    }
    if (cfg.discoveryPort >= 0) {
        if (d.udp.bind(cfg.bindAddress, cfg.discoveryPort, &err)) {
            d.udpPort = d.udp.localPort();
        }
    }
    d.acceptThread = std::thread([&d] { d.acceptLoop(); });
    if (d.udpPort >= 0) d.udpThread = std::thread([&d] { d.udpLoop(); });
    return true;
}

void HostLink::stop() {
    if (!d_) return;
    auto& d = *d_;
    d.stopping = true;
    if (d.acceptThread.joinable()) d.acceptThread.join();
    if (d.udpThread.joinable()) d.udpThread.join();
    std::map<int, std::shared_ptr<Conn>> all;
    {
        std::lock_guard<std::mutex> lock(d.connsMutex);
        all.swap(d.conns);
    }
    // Ce qui attend part d'abord (un AUREVOIR) : une seconde au plus.
    const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    for (auto& [id, c] : all) {
        (void)id;
        while (!c->done && Clock::now() < deadline) {
            bool empty;
            {
                std::lock_guard<std::mutex> lock(c->m);
                empty = c->out.empty();
            }
            if (empty) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        c->stop = true;
    }
    for (auto& [id, c] : all) {
        (void)id;
        if (c->th.joinable()) c->th.join();
    }
    d.listener.close();
    d.udp.close();
    d.tcpPort = d.udpPort = -1;
}

bool HostLink::running() const noexcept { return d_ && d_->tcpPort >= 0 && !d_->stopping; }
int HostLink::port() const noexcept { return d_ ? d_->tcpPort : -1; }
int HostLink::discoveryPort() const noexcept { return d_ ? d_->udpPort : -1; }

void HostLink::setAnnounce(const Announce& a) {
    std::lock_guard<std::mutex> lock(d_->announceMutex);
    d_->announce = a;
    d_->announce.port = d_->tcpPort;
}

std::vector<LinkEvent> HostLink::poll() {
    std::vector<LinkEvent> out;
    std::lock_guard<std::mutex> lock(d_->eventsMutex);
    out.swap(d_->events);
    return out;
}

void HostLink::send(int conn, const core::json::Value& header, std::string data) {
    if (auto c = d_->find(conn)) c->push(header, std::move(data));
}

void HostLink::closeAfter(int conn, const core::json::Value& header) {
    if (auto c = d_->find(conn)) c->push(header, {}, true);
}

void HostLink::close(int conn) {
    if (auto c = d_->find(conn)) c->stop = true;
}

std::size_t HostLink::pending(int conn) const {
    if (auto c = d_->find(conn)) {
        std::lock_guard<std::mutex> lock(c->m);
        return c->outBytes;
    }
    return 0;
}

// =========================================================================================== l'equipier ===
struct GuestLink::Impl {
    GuestConfig cfg;
    deploy::TlsIdentity identity;
    std::shared_ptr<Conn> conn;
    std::thread th;
    std::mutex eventsMutex;
    std::vector<LinkEvent> events;
    std::string hostFp;
    std::mutex fpMutex;

    void post(LinkEvent e) {
        std::lock_guard<std::mutex> lock(eventsMutex);
        events.push_back(std::move(e));
    }

    void run() {
        auto& c = *conn;
        std::string why;
        LinkEvent closed;
        closed.kind = LinkEvent::Kind::Closed;
        const auto end = [&](std::string reason) {
            closed.why = std::move(reason);
            c.tls.close();
            c.done = true;
            post(std::move(closed));
        };
        c.tls.setCancel(&c.stop);
        if (!c.tls.connect(cfg.address, cfg.port, identity, 6000, &why)) { end("connexion \xC3\xA0 " + cfg.address + " impossible : " + why); return; }
        const std::string fp = c.tls.peerFingerprintHex();
        {
            std::lock_guard<std::mutex> lock(fpMutex);
            hostFp = fp;
        }
        if (!cfg.fingerprint.empty() && fp != cfg.fingerprint) {
            end("le PC qui r\xC3\xA9pond n'est pas celui de la session (son certificat n'est pas celui annonc\xC3\xA9) : rien n'a \xC3\xA9t\xC3\xA9 envoy\xC3\xA9");
            return;
        }
        core::json::Value hello = cfg.hello.isObject() ? cfg.hello : core::json::Value::object();
        hello.set("op", "BONJOUR");
        hello.set("proto", kProtocol);
        hello.set("sid", cfg.sessionId);
        if (!sendOne(c, hello, &why)) { end(why); return; }
        Frame f;
        if (!readOne(c, f, kHandshakeMs, &why)) { end("pas de r\xC3\xA9ponse de l'h\xC3\xB4te : " + why); return; }
        if (f.header.str("op") == "CODE_DEMANDE") {
            if (codeTyped(cfg.code).size() != static_cast<std::size_t>(kCodeDigits)) {
                core::json::Value r = op("REFUS");
                r.set("raison", "la session demande son code (6 chiffres)");
                r.set("il_faut_le_code", true);
                LinkEvent e;
                e.kind = LinkEvent::Kind::Frame;
                e.header = r;
                post(std::move(e));
                end("il faut le code");
                return;
            }
            deploy::pairing::StudioPairing sp(codeTyped(cfg.code), identity.fingerprint(), c.tls.peerFingerprint(),
                                              hello.str("nom", "?").empty() ? std::string("?") : hello.str("nom", "?"),
                                              sessionBinding(cfg.sessionId));
            core::json::Value start;
            if (!sp.start(start, &why)) { end("code : " + why); return; }
            core::json::Value h = op("CODE_DEBUT");
            h.set("cpace", std::move(start));
            if (!sendOne(c, h, &why)) { end(why); return; }
            if (!readOne(c, f, 120000, &why)) { end("code : " + why); return; }
            if (f.header.str("op") == "CODE_REPONSE") {
                core::json::Value fin;
                if (!sp.answer(f.header.at("cpace"), fin, &why) || sp.agentId() != cfg.sessionId) {
                    // L'hote ne prouve pas le code : un intermediaire, ou un code faux. Rien de plus ne part.
                    core::json::Value r = op("REFUS");
                    r.set("raison", "code faux (ou un autre PC s'interpose) : v\xC3\xA9rifie les 6 chiffres");
                    r.set("code_faux", true);
                    LinkEvent e;
                    e.kind = LinkEvent::Kind::Frame;
                    e.header = r;
                    post(std::move(e));
                    end("code faux");
                    return;
                }
                core::json::Value h2 = op("CODE_FIN");
                h2.set("cpace", std::move(fin));
                if (!sendOne(c, h2, &why)) { end(why); return; }
                if (!readOne(c, f, kHandshakeMs, &why)) { end("pas de r\xC3\xA9ponse de l'h\xC3\xB4te : " + why); return; }
            }
        }
        if (f.header.str("op") == "REFUS") {
            LinkEvent e;
            e.kind = LinkEvent::Kind::Frame;
            e.header = std::move(f.header);
            post(std::move(e));
            end("refus\xC3\xA9");
            return;
        }
        if (f.header.str("op") != "LIEN_OK") { end("r\xC3\xA9ponse inattendue de l'h\xC3\xB4te"); return; }
        LinkEvent ok;
        ok.kind = LinkEvent::Kind::Connected;
        ok.fingerprint = fp;
        ok.address = cfg.address;
        ok.codeOk = f.header.flag("code");
        post(std::move(ok));
        const std::string reason = pump(c, [&](Frame&& fr) {
            LinkEvent e;
            e.kind = LinkEvent::Kind::Frame;
            e.header = std::move(fr.header);
            e.data = std::move(fr.data);
            post(std::move(e));
        });
        end(reason);
    }
};

GuestLink::GuestLink() : d_(std::make_unique<Impl>()) {}
GuestLink::~GuestLink() { stop(); }

bool GuestLink::start(const GuestConfig& cfg, const deploy::TlsIdentity& identity, std::string* why) {
    stop();
    d_ = std::make_unique<Impl>();
    if (!deploy::tls::ready(why)) return false;
    if (!deploy::net::startup()) {
        if (why) *why = "le r\xC3\xA9seau est indisponible";
        return false;
    }
    d_->cfg = cfg;
    d_->identity = identity;
    d_->conn = std::make_shared<Conn>();
    auto* d = d_.get();
    d_->th = std::thread([d] { d->run(); });
    return true;
}

void GuestLink::stop() {
    if (!d_ || !d_->conn) return;
    auto& c = *d_->conn;
    const auto deadline = Clock::now() + std::chrono::milliseconds(1000);
    while (!c.done && Clock::now() < deadline) {
        bool empty;
        {
            std::lock_guard<std::mutex> lock(c.m);
            empty = c.out.empty();
        }
        if (empty) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    c.stop = true;
    if (d_->th.joinable()) d_->th.join();
    d_->conn.reset();
}

bool GuestLink::running() const noexcept { return d_ && d_->conn && !d_->conn->done; }

std::vector<LinkEvent> GuestLink::poll() {
    std::vector<LinkEvent> out;
    std::lock_guard<std::mutex> lock(d_->eventsMutex);
    out.swap(d_->events);
    return out;
}

void GuestLink::send(const core::json::Value& header, std::string data) {
    if (d_->conn) d_->conn->push(header, std::move(data));
}

void GuestLink::closeAfter(const core::json::Value& header) {
    if (d_->conn) d_->conn->push(header, {}, true);
}

std::string GuestLink::hostFingerprint() const {
    std::lock_guard<std::mutex> lock(d_->fpMutex);
    return d_->hostFp;
}

} // namespace session
