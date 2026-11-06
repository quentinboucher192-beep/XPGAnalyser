// =============================================================================
//  deploy/Discovery.cpp - 1.28.0 : la decouverte UDP (voir Discovery.hpp)
// =============================================================================
#include "Discovery.hpp"

#include "Crypto.hpp"
#include "Net.hpp"

#include <algorithm>
#include <set>

namespace deploy::discovery {

namespace {
bool header(const core::json::Value& v, std::string_view op, std::string& nonce) {
    if (!v.isObject() || v.integer("xpgd") != 1 || v.str("op") != op) return false;
    const auto& n = v.at("nonce");
    if (!n.isString() || n.asString().empty() || n.asString().size() > 64) return false;
    nonce = n.asString();
    return true;
}
} // namespace

std::string discoverMessage(const std::string& nonce) {
    core::json::Value v = core::json::Value::object();
    v.set("xpgd", 1);
    v.set("op", "DISCOVER");
    v.set("nonce", nonce);
    return core::json::write(v, false);
}

bool parseDiscover(std::string_view text, std::string& nonce) {
    if (text.size() > 1024) return false;
    core::json::Value v;
    return core::json::parse(text, v, nullptr) && header(v, "DISCOVER", nonce);
}

std::string hereMessage(const std::string& nonce, const Announce& a) {
    core::json::Value v = core::json::Value::object();
    v.set("xpgd", 1);
    v.set("op", "HERE");
    v.set("nonce", nonce);
    v.set("machine", a.machine);
    v.set("id", a.id);
    v.set("version", a.version);
    core::json::Value protos = core::json::Value::array();
    for (const int p : a.protos) protos.push(p);
    v.set("protos", std::move(protos));
    v.set("port", a.port);
    v.set("fp", a.fingerprint);
    return core::json::write(v, false);
}

bool parseHere(std::string_view text, std::string& nonce, Announce& a) {
    if (text.size() > 1500) return false;
    core::json::Value v;
    if (!core::json::parse(text, v, nullptr) || !header(v, "HERE", nonce)) return false;
    a.machine = v.str("machine").substr(0, 128);
    a.id = v.str("id").substr(0, 32);
    a.version = v.str("version").substr(0, 32);
    a.fingerprint = v.str("fp");
    crypto::Bytes b;
    if (!crypto::unhex(a.fingerprint, b, 32)) a.fingerprint.clear();
    a.port = static_cast<int>(v.integer("port", protocol::kAgentPort));
    if (a.port <= 0 || a.port > 65535) return false;
    a.protos.clear();
    for (const auto& p : v.at("protos").items())
        if (p.isNumber() && a.protos.size() < 16) a.protos.push_back(static_cast<int>(p.asInt()));
    return true;
}

std::vector<Found> discover(const Options& o, std::string* why) {
    std::vector<Found> out;
    net::UdpSocket sock;
    std::string err;
    if (!sock.bind("0.0.0.0", 0, &err)) {
        if (why) *why = "d\xC3\xA9" "couverte impossible : " + err;
        return out;
    }
    (void)sock.enableBroadcast();
    unsigned char raw[8];
    crypto::randomBytes(raw, sizeof raw);
    const std::string nonce = crypto::hex(raw, sizeof raw);
    const std::string msg = discoverMessage(nonce);
    std::vector<std::string> targets = o.direct;
    if (o.broadcast) {
        targets.emplace_back("255.255.255.255");
        for (const auto& b : net::broadcastAddresses()) targets.push_back(b);
    }
    const auto sendAll = [&]() {
        int sent = 0;
        for (const auto& t : targets)
            if (sock.sendTo(t, o.port, msg, &err)) ++sent;
        return sent;
    };
    if (sendAll() == 0) {
        if (why) *why = targets.empty() ? "aucune adresse \xC3\xA0 interroger" : "d\xC3\xA9" "couverte impossible : " + err;
        return out;
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(std::max(50, o.waitMs));
    bool resent = false;   // un second envoi, au tiers du delai : un datagramme peut se perdre
    std::set<std::string> seen;
    for (;;) {
        const auto now = Clock::now();
        if (now >= deadline) break;
        if (!resent && now - start >= (deadline - start) / 3) {
            resent = true;
            (void)sendAll();
        }
        const int left = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        net::Datagram d;
        const int rc = sock.receive(d, std::min(left, 100));
        if (rc < 0) break;
        if (rc == 0) continue;
        std::string n;
        Announce a;
        if (!parseHere(d.data, n, a) || n != nonce) continue;
        const std::string key = a.fingerprint.empty() ? d.from + ":" + std::to_string(a.port) : a.fingerprint;
        if (!seen.insert(key).second) continue;
        Found f;
        f.address = d.from;
        f.port = a.port;
        f.machine = a.machine;
        f.id = a.id;
        f.version = a.version;
        f.fingerprint = a.fingerprint;
        f.protos = a.protos;
        out.push_back(std::move(f));
    }
    return out;
}

bool RateLimiter::allow(const std::string& sender, const std::string& nonce, std::chrono::steady_clock::time_point now) {
    using namespace std::chrono;
    // Le menage : les traces de plus de 5 s.
    if (nonces_.size() > 512 || senders_.size() > 512) {
        for (auto it = nonces_.begin(); it != nonces_.end();) it = now - it->second > seconds(5) ? nonces_.erase(it) : std::next(it);
        for (auto it = senders_.begin(); it != senders_.end();)
            it = now - it->second.windowStart > seconds(5) ? senders_.erase(it) : std::next(it);
        if (nonces_.size() > 2048) nonces_.clear();
        if (senders_.size() > 2048) senders_.clear();
    }
    const std::string key = sender + "|" + nonce;
    if (const auto it = nonces_.find(key); it != nonces_.end() && now - it->second < seconds(5)) return false;
    auto& s = senders_[sender];
    if (now - s.windowStart >= seconds(1)) {
        s.windowStart = now;
        s.count = 0;
    }
    if (s.count >= 5) return false;
    if (!started_) {
        started_ = true;
        last_ = now;
    }
    tokens_ = std::min(20.0, tokens_ + duration<double>(now - last_).count() * 10.0);
    last_ = now;
    if (tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    ++s.count;
    nonces_[key] = now;
    return true;
}

} // namespace deploy::discovery
