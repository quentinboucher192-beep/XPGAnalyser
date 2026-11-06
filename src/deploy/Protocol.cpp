// =============================================================================
//  deploy/Protocol.cpp - 1.28.0 : le protocole (voir Protocol.hpp)
// =============================================================================
#include "Protocol.hpp"

#include "Crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace deploy::protocol {

namespace {
// Un nombre entier exact (pas de 7.5 pour seq), sinon faux.
bool integer(const core::json::Value& v, std::int64_t& out) {
    if (!v.isNumber()) return false;
    const double d = v.asNumber();
    if (!std::isfinite(d) || d != std::floor(d) || std::fabs(d) > 9.0e15) return false;
    out = static_cast<std::int64_t>(d);
    return true;
}
} // namespace

const std::vector<int>& acceptedVersions() {
    static const std::vector<int> v{1, 2};
    return v;
}

bool isDeployOp(std::string_view op) {
    return op == kPrepare || op == kSendChunk || op == kVerifyPackage || op == kInstall || op == kStop || op == kStart || op == kRollback
           || op == kListApps || op == kGetHistory;
}

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string newOperationId() {
    unsigned char b[4];
    crypto::randomBytes(b, sizeof b);
    return "op-" + crypto::hex(b, sizeof b);
}

int chooseVersion(const std::vector<int>& ours, const std::vector<int>& theirs) {
    int best = 0;
    for (const int v : ours)
        if (v > best && std::find(theirs.begin(), theirs.end(), v) != theirs.end()) best = v;
    return best;
}

core::json::Value requestHeader(std::string_view op, const std::string& id, std::int64_t seq, std::int64_t ts, int proto) {
    core::json::Value h = core::json::Value::object();
    h.set("proto", proto);
    h.set("op", std::string(op));
    h.set("id", id);
    h.set("seq", seq);
    h.set("ts", ts);
    return h;
}

core::json::Value okHeader(std::string_view op, const std::string& re, std::int64_t seq, int proto) {
    core::json::Value h = core::json::Value::object();
    h.set("proto", proto);
    h.set("op", std::string(op));
    h.set("re", re);
    h.set("seq", seq);
    h.set("ok", true);
    return h;
}

core::json::Value errorHeader(std::string_view op, const std::string& re, std::int64_t seq, std::string_view code,
                              const std::string& message, int proto) {
    core::json::Value h = core::json::Value::object();
    h.set("proto", proto);
    h.set("op", std::string(op));
    h.set("re", re);
    h.set("seq", seq);
    h.set("ok", false);
    h.set("error", std::string(code));
    h.set("message", message);
    return h;
}

bool parseRequest(const core::json::Value& header, Request& out, std::string& message) {
    std::int64_t proto = 0;
    if (!header.isObject()) {
        message = "l'en-t\xC3\xAAte n'est pas un objet JSON";
        return false;
    }
    if (!integer(header.at("proto"), proto) || proto <= 0 || proto > 1000) {
        message = "\"proto\" manque ou n'est pas un entier";
        return false;
    }
    const auto& op = header.at("op");
    const auto& id = header.at("id");
    if (!op.isString() || op.asString().empty() || op.asString().size() > 64) {
        message = "\"op\" manque";
        return false;
    }
    if (!id.isString() || id.asString().empty() || id.asString().size() > 64) {
        message = "\"id\" (l'identifiant d'op\xC3\xA9ration) manque";
        return false;
    }
    if (!integer(header.at("seq"), out.seq) || out.seq <= 0) {
        message = "\"seq\" manque ou n'est pas un entier positif";
        return false;
    }
    if (!integer(header.at("ts"), out.ts)) {
        message = "\"ts\" manque ou n'est pas un entier";
        return false;
    }
    out.proto = static_cast<int>(proto);
    out.op = op.asString();
    out.id = id.asString();
    return true;
}

bool parseReply(const core::json::Value& header, Reply& out, std::string& message) {
    if (!header.isObject() || !header.at("ok").isBool() || !header.at("op").isString()) {
        message = "en-t\xC3\xAAte de r\xC3\xA9ponse illisible";
        return false;
    }
    out.ok = header.flag("ok");
    out.replay = header.flag("replay");
    out.op = header.str("op");
    out.re = header.str("re");
    out.error = header.str("error");
    out.message = header.str("message");
    (void)integer(header.at("seq"), out.seq);
    return true;
}

bool ReplayGuard::accept(std::int64_t seq, std::int64_t ts, std::int64_t now, std::string& code, std::string& message) {
    if (seq <= last_) {
        code = std::string(err::kReplay);
        message = "demande refus\xC3\xA9" "e : son num\xC3\xA9ro (" + std::to_string(seq) + ") ne suit pas le pr\xC3\xA9" "c\xC3\xA9" "dent ("
                  + std::to_string(last_) + ") - une demande rejou\xC3\xA9" "e ?";
        return false;
    }
    const std::int64_t gap = ts > now ? ts - now : now - ts;
    if (gap > kClockWindowMs) {
        code = std::string(err::kClock);
        message = "demande refus\xC3\xA9" "e : son heure est \xC3\xA0 " + std::to_string(gap / 1000)
                  + " s de celle de l'agent (5 minutes au plus) - r\xC3\xA8gle l'horloge d'un des deux PC";
        return false;
    }
    last_ = seq;
    return true;
}

bool ResponseCache::find(const std::string& peer, const std::string& id, Frame& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = byKey_.find(peer + "|" + id);
    if (it == byKey_.end()) return false;
    out = it->second;
    return true;
}

void ResponseCache::put(const std::string& peer, const std::string& id, const Frame& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = peer + "|" + id;
    if (byKey_.count(key)) return;
    byKey_[key] = response;
    order_.push_back(key);
    while (order_.size() > capacity_) {
        byKey_.erase(order_.front());
        order_.pop_front();
    }
}

} // namespace deploy::protocol
