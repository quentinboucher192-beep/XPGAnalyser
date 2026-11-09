// =============================================================================
//  app/SessionRelayHttp.cpp - 1.44.0 : voir SessionRelayHttp.hpp
// =============================================================================
#include "SessionRelayHttp.hpp"

#include "ai/AiHttp.hpp"

#include "../core/Json.hpp"
#include "../licensing/Service.hpp"
#include "../licensing/Time.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

namespace app::sessionrelay {

namespace {

constexpr std::size_t kMaxReply = 16u * 1024u * 1024u;   // une lecture du relais : 4 Mio de base64 et le JSON

class HttpTransport final : public session::RelayTransport {
public:
    explicit HttpTransport(HttpOptions o) : o_(std::move(o)) {
        while (!o_.baseUrl.empty() && o_.baseUrl.back() == '/') o_.baseUrl.pop_back();
    }

    session::RelayReply call(const session::RelayCall& c) override {
        session::RelayReply r;
        if (cancelled_) return stopped();
        if (o_.baseUrl.empty()) {
            r.error = "ce programme n'a pas de service XPG (l'adresse du service des licences)";
            return r;
        }
        if (!offsetKnown_) (void)readOffset();
        r = once(c);
        if (r.status == 401 && !cancelled_) {
            core::json::Value v;
            (void)core::json::parse(r.body, v);
            const std::string e = v.str("erreur");
            if (e == "heure" || e == "date") {
                const long long before = offset_.load();
                if (readOffset() && offset_.load() != before) r = once(c);
                else if (e == "heure") r = once(c);   // le relais vient d'etre relance : signe de nouveau, une fois
            }
        }
        return r;
    }

    void cancel() override {
        cancelled_ = true;
        std::lock_guard<std::mutex> lock(m_);
        for (auto* cl : active_) cl->cancel();
    }

private:
    session::RelayReply stopped() const {
        session::RelayReply r;
        r.error = "arr\xC3\xAAt";
        return r;
    }

    // Une requete HTTP (bloquante), annulable par cancel().
    session::RelayReply perform(const ai::HttpRequest& req) {
        session::RelayReply r;
        auto client = ai::makeHttpClient();
        {
            std::lock_guard<std::mutex> lock(m_);
            if (cancelled_) return stopped();
            active_.insert(client.get());
        }
        bool tooBig = false;
        ai::HttpSink sink;
        sink.onData = [&](std::string_view chunk) {
            if (r.body.size() + chunk.size() > kMaxReply) tooBig = true;
            else r.body.append(chunk);
        };
        const ai::HttpResponse res = client->perform(req, sink);
        {
            std::lock_guard<std::mutex> lock(m_);
            active_.erase(client.get());
        }
        if (!res.ok() || tooBig) {
            ai::Url u;
            r.error = tooBig ? std::string("la r\xC3\xA9ponse du service XPG est trop grosse")
                             : (res.error.empty() ? ai::failureText(res.failure, ai::parseUrl(req.url, u) ? u.host : std::string("le service XPG")) : res.error);
            r.body.clear();
            return r;
        }
        r.status = res.status;
        return r;
    }

    // L'ecart d'heure, lu sur GET /v1/etat (public).
    bool readOffset() {
        ai::HttpRequest req;
        req.method = "GET";
        req.url = o_.baseUrl + "/v1/etat";
        req.headers = {{"Cache-Control", "no-cache"}};
        req.connectTimeoutSeconds = 15;
        req.idleTimeoutSeconds = 20;
        const auto r = perform(req);
        core::json::Value v;
        if (r.status != 200 || !core::json::parse(r.body, v)) return false;
        const auto heure = v.integer("heure");
        if (heure <= 0) return false;
        offset_ = static_cast<long long>(heure - licensing::systemNow());
        offsetKnown_ = true;
        return true;
    }

    session::RelayReply once(const session::RelayCall& c) {
        session::RelayReply r;
        deploy::crypto::PublicKey pk{};
        std::string why;
        if (!o_.publicKey || !o_.sign || !o_.publicKey(pk, why)) {
            r.error = "la cl\xC3\xA9 de ce PC n'est pas lisible" + (why.empty() ? std::string{} : " : " + why);
            return r;
        }
        bool signedOk = true;
        const auto headers = licensing::service::signRequest(
            c.method, c.path, c.body, pk,
            [&](std::string_view m) {
                deploy::crypto::Signature sig{};
                std::string w;
                if (!o_.sign(m, sig, w)) {
                    signedOk = false;
                    why = w;
                }
                return sig;
            },
            licensing::systemNow() + static_cast<licensing::Seconds>(offset_.load()));
        if (!signedOk) {
            r.error = "l'appel n'a pas pu \xC3\xAAtre sign\xC3\xA9 par ce PC" + (why.empty() ? std::string{} : " : " + why);
            return r;
        }
        ai::HttpRequest req;
        req.method = c.method;
        req.url = o_.baseUrl + c.path;
        req.headers = {{"Cache-Control", "no-cache"}};
        if (!c.body.empty() || c.method != "GET") req.headers.emplace_back("Content-Type", "application/json");
        for (const auto& h : headers) req.headers.push_back(h);
        if (!o_.software.empty()) req.headers.emplace_back("X-XPG-Logiciel", o_.software);
        req.body = c.body;
        req.connectTimeoutSeconds = 15;
        req.idleTimeoutSeconds = c.waitSeconds + 20;   // l'attente longue du relais, et de la marge
        return perform(req);
    }

    HttpOptions o_;
    std::atomic<bool> cancelled_{false};
    std::atomic<long long> offset_{0};
    std::atomic<bool> offsetKnown_{false};
    std::mutex m_;
    std::set<ai::HttpClient*> active_;
};

} // namespace

session::RelayTransportPtr httpTransport(HttpOptions options) { return std::make_shared<HttpTransport>(std::move(options)); }

} // namespace app::sessionrelay
