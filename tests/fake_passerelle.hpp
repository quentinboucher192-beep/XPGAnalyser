// =============================================================================
//  tests/fake_passerelle.hpp - 1.34.0 : UN FAUX GITHUB POUR LA PASSERELLE DES
//  LICENCES, dans le processus de l'essai (le transport injecte :
//  app::ai::setHttpFactory) - aucun reseau
// -----------------------------------------------------------------------------
//  Le depot PUBLIC de la passerelle :
//    - les fichiers (raw : GET <raw>/<depot>/<branche>/<chemin>, sans jeton) ;
//    - l'API Contents (GET avec ou sans jeton ; PUT et DELETE : le jeton de
//      l'EDITEUR seulement, le sha exige pour remplacer, 409 sinon) ;
//    - les issues (POST, GET liste paginee - et filtree par since : celles dont
//      updated_at >= since, comme GitHub -, GET une, PATCH l'etat ou le corps,
//      les commentaires : POST et GET) : le jeton des DEMANDES ou celui de
//      l'editeur ; sans jeton : 401 pour ecrire ;
//    - GET <api>/repos/<depot> : public, et ce que le jeton peut faire.
//  Des pannes a la demande : hors ligne (Connect), un code d'erreur pour les N
//  requetes suivantes, la limite de GitHub atteinte (403 + x-ratelimit-remaining 0).
//  Tout est note (requests, writes) : un essai verifie qu'aucun nom de client ni
//  aucun jeton ne passe en clair.
// =============================================================================
#pragma once

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fakegh {

namespace json = core::json;

struct Comment {
    std::int64_t id{0};
    std::string  body, user;
    std::string  createdAt;
};

struct Issue {
    int                  number{0};
    std::string          title, body, state{"open"}, user;
    std::string          createdAt, updatedAt;
    std::vector<Comment> comments;
};

struct Answer {
    int         status{200};
    std::string body;
    app::ai::HttpHeaders headers;
};

struct FakePasserelle {
    std::string api{"https://api.github.com"};
    std::string raw{"https://raw.githubusercontent.com"};
    std::string repo{"editeur-essai/passerelle-essai"};
    std::string branch{"main"};
    std::string adminToken{"essai_jeton_EDITEUR_000000000000000000000"};
    std::string requestToken{"essai_jeton_DEMANDES_00000000000000000000"};
    std::string owner{"editeur-essai"};

    std::map<std::string, std::string> files;   // chemin -> contenu
    std::map<std::string, std::string> shas;    // chemin -> sha (faux, change a chaque ecriture)
    std::vector<Issue>                 issues;
    int                                commits{0};
    std::int64_t                       nextCommentId{1000};
    int                                clockTick{0};

    // Les pannes.
    bool        offline{false};
    int         failCount{0};      // les N requetes suivantes rendent failStatus
    int         failStatus{500};
    bool        rateLimited{false};

    // Ce qui est passe.
    std::vector<std::string> requests;   // "METHODE url -> code"
    std::vector<std::string> writes;     // les corps envoyes (POST, PUT, PATCH)
    std::vector<std::string> authSeen;   // les jetons vus (pour l'essai : jamais dans une URL)
    std::mutex               mutex;

    [[nodiscard]] std::string now() {
        ++clockTick;
        char b[32];
        std::snprintf(b, sizeof b, "2026-11-07T10:%02d:%02dZ", (clockTick / 60) % 60, clockTick % 60);
        return b;
    }

    [[nodiscard]] std::string tokenOf(const app::ai::HttpRequest& req) const {
        for (const auto& [k, v] : req.headers) {
            std::string key = k;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (key != "authorization") continue;
            if (v.rfind("Bearer ", 0) == 0) return v.substr(7);
            if (v.rfind("token ", 0) == 0) return v.substr(6);
        }
        return {};
    }

    static Answer error(int status, const std::string& message) {
        json::Value v = json::Value::object();
        v.set("message", message);
        return {status, json::write(v, false), {}};
    }

    [[nodiscard]] json::Value issueJson(const Issue& i) const {
        json::Value v = json::Value::object();
        v.set("number", i.number);
        v.set("title", i.title);
        v.set("body", i.body);
        v.set("state", i.state);
        v.set("comments", static_cast<int>(i.comments.size()));
        v.set("created_at", i.createdAt);
        v.set("updated_at", i.updatedAt);
        v.set("html_url", "https://github.com/" + repo + "/issues/" + std::to_string(i.number));
        json::Value u = json::Value::object();
        u.set("login", i.user);
        v.set("user", u);
        return v;
    }

    Issue* findIssue(int n) {
        for (auto& i : issues)
            if (i.number == n) return &i;
        return nullptr;
    }

    static std::string queryValue(const std::string& query, const std::string& key) {
        std::size_t pos = 0;
        while (pos < query.size()) {
            const auto amp = query.find('&', pos);
            const std::string part = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            const auto eq = part.find('=');
            if (eq != std::string::npos && part.substr(0, eq) == key) return part.substr(eq + 1);
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
        return {};
    }

    Answer handle(const app::ai::HttpRequest& req) {
        if (failCount > 0) {
            --failCount;
            return error(failStatus, "panne d'essai");
        }
        if (rateLimited) {
            Answer a = error(403, "API rate limit exceeded");
            a.headers.push_back({"x-ratelimit-remaining", "0"});
            return a;
        }
        const std::string token = tokenOf(req);
        if (!token.empty()) authSeen.push_back(token);
        const bool isAdmin = token == adminToken, isRequester = token == requestToken;
        if (!token.empty() && !isAdmin && !isRequester) return error(401, "Bad credentials");

        // ---- raw ----
        const std::string rawPrefix = raw + "/" + repo + "/" + branch + "/";
        if (req.url.rfind(rawPrefix, 0) == 0) {
            if (req.method != "GET") return error(405, "raw : GET seulement");
            const std::string path = req.url.substr(rawPrefix.size());
            const auto it = files.find(path);
            if (it == files.end()) return {404, "404: Not Found", {}};
            return {200, it->second, {}};
        }

        // ---- api ----
        const std::string apiPrefix = api + "/repos/" + repo;
        if (req.url.rfind(apiPrefix, 0) != 0) return error(404, "Not Found");
        std::string tail = req.url.substr(apiPrefix.size());
        std::string query;
        if (const auto q = tail.find('?'); q != std::string::npos) {
            query = tail.substr(q + 1);
            tail = tail.substr(0, q);
        }
        json::Value body;
        if (!req.body.empty()) {
            writes.push_back(req.method + " " + tail + " " + req.body);
            if (!json::parse(req.body, body)) return error(400, "Problems parsing JSON");
        }

        if (tail.empty() && req.method == "GET") {
            json::Value v = json::Value::object();
            v.set("full_name", repo);
            v.set("private", false);
            v.set("default_branch", branch);
            json::Value p = json::Value::object();
            p.set("pull", true);
            p.set("push", isAdmin);
            p.set("admin", isAdmin);
            v.set("permissions", p);
            return {200, json::write(v, false), {}};
        }

        // Contents
        if (tail.rfind("/contents/", 0) == 0) {
            const std::string path = tail.substr(10);
            if (req.method == "GET") {
                const auto it = files.find(path);
                if (it == files.end()) return error(404, "Not Found");
                json::Value v = json::Value::object();
                v.set("type", "file");
                v.set("path", path);
                v.set("sha", shas[path]);
                v.set("encoding", "base64");
                v.set("content", deploy::crypto::base64(it->second));
                return {200, json::write(v, false), {}};
            }
            if (req.method == "PUT" || req.method == "DELETE") {
                if (token.empty()) return error(401, "Requires authentication");
                if (!isAdmin) return error(403, "Resource not accessible by personal access token");
                if (body.str("branch", branch) != branch) return error(404, "Branch not found");
                const auto it = files.find(path);
                const std::string sha = body.str("sha");
                if (it != files.end() && sha != shas[path]) return error(409, path + " does not match " + sha);
                if (it == files.end() && !sha.empty() && req.method == "PUT") return error(422, "sha wasn't supplied");
                if (req.method == "DELETE") {
                    if (it == files.end()) return error(404, "Not Found");
                    files.erase(path);
                    shas.erase(path);
                    ++commits;
                    return {200, "{}", {}};
                }
                deploy::crypto::Bytes content;
                if (!deploy::crypto::unbase64(body.str("content"), content)) return error(422, "content is not valid Base64");
                const bool created = it == files.end();
                files[path] = std::string(content.begin(), content.end());
                ++commits;
                shas[path] = "sha" + std::to_string(commits);
                json::Value v = json::Value::object();
                json::Value c = json::Value::object();
                c.set("sha", shas[path]);
                c.set("path", path);
                v.set("content", c);
                json::Value cm = json::Value::object();
                cm.set("sha", "commit" + std::to_string(commits));
                v.set("commit", cm);
                return {created ? 201 : 200, json::write(v, false), {}};
            }
            return error(405, "methode");
        }

        // Issues
        if (tail == "/issues") {
            if (req.method == "POST") {
                if (token.empty()) return error(401, "Requires authentication");
                Issue i;
                i.number = static_cast<int>(issues.size()) + 1;
                i.title = body.str("title");
                i.body = body.str("body");
                i.user = owner;
                i.createdAt = i.updatedAt = now();
                if (i.title.empty()) return error(422, "title missing");
                issues.push_back(i);
                return {201, json::write(issueJson(i), false), {}};
            }
            if (req.method == "GET") {
                const std::string state = queryValue(query, "state").empty() ? "open" : queryValue(query, "state");
                const int perPage = std::clamp(std::atoi(queryValue(query, "per_page").c_str()) > 0 ? std::atoi(queryValue(query, "per_page").c_str()) : 30, 1, 100);
                const int page = std::max(1, std::atoi(queryValue(query, "page").c_str()));
                const std::string since = queryValue(query, "since");   // "AAAA-MM-JJTHH:MM:SSZ" : se compare en texte
                std::vector<const Issue*> list;
                for (auto it = issues.rbegin(); it != issues.rend(); ++it)   // les plus recentes d'abord (comme GitHub)
                    if ((state == "all" || it->state == state) && (since.empty() || it->updatedAt >= since)) list.push_back(&*it);
                json::Value arr = json::Value::array();
                for (std::size_t k = static_cast<std::size_t>((page - 1) * perPage); k < list.size() && k < static_cast<std::size_t>(page * perPage); ++k)
                    arr.push(issueJson(*list[k]));
                return {200, json::write(arr, false), {}};
            }
            return error(405, "methode");
        }
        if (tail.rfind("/issues/", 0) == 0) {
            std::string rest = tail.substr(8);
            const auto slash = rest.find('/');
            const int n = std::atoi(rest.substr(0, slash).c_str());
            Issue* i = findIssue(n);
            if (!i) return error(404, "Not Found");
            if (slash == std::string::npos) {
                if (req.method == "GET") return {200, json::write(issueJson(*i), false), {}};
                if (req.method == "PATCH") {
                    if (token.empty()) return error(401, "Requires authentication");
                    if (body.has("state")) i->state = body.str("state");
                    if (body.has("body")) i->body = body.str("body");
                    i->updatedAt = now();
                    return {200, json::write(issueJson(*i), false), {}};
                }
                return error(405, "methode");
            }
            if (rest.substr(slash) == "/comments") {
                if (req.method == "POST") {
                    if (token.empty()) return error(401, "Requires authentication");
                    Comment c;
                    c.id = nextCommentId++;
                    c.body = body.str("body");
                    c.user = owner;
                    c.createdAt = now();
                    i->comments.push_back(c);
                    i->updatedAt = c.createdAt;
                    json::Value v = json::Value::object();
                    v.set("id", c.id);
                    v.set("body", c.body);
                    return {201, json::write(v, false), {}};
                }
                if (req.method == "GET") {
                    json::Value arr = json::Value::array();
                    for (const auto& c : i->comments) {
                        json::Value v = json::Value::object();
                        v.set("id", c.id);
                        v.set("body", c.body);
                        v.set("created_at", c.createdAt);
                        json::Value u = json::Value::object();
                        u.set("login", c.user);
                        v.set("user", u);
                        arr.push(v);
                    }
                    return {200, json::write(arr, false), {}};
                }
            }
            return error(405, "methode");
        }
        return error(404, "Not Found");
    }

    // ---- pour les essais ----
    [[nodiscard]] bool anyWriteContains(const std::string& needle) const {
        return std::any_of(writes.begin(), writes.end(), [&](const std::string& w) { return w.find(needle) != std::string::npos; });
    }
    [[nodiscard]] bool anyPublicContains(const std::string& needle) const {
        for (const auto& [p, c] : files)
            if (c.find(needle) != std::string::npos) return true;
        for (const auto& i : issues) {
            if (i.title.find(needle) != std::string::npos || i.body.find(needle) != std::string::npos) return true;
            for (const auto& c : i.comments)
                if (c.body.find(needle) != std::string::npos) return true;
        }
        return false;
    }
};

inline FakePasserelle*& current() {
    static FakePasserelle* f = nullptr;
    return f;
}

class Client final : public app::ai::HttpClient {
public:
    app::ai::HttpResponse perform(const app::ai::HttpRequest& req, const app::ai::HttpSink& sink) override {
        app::ai::HttpResponse r;
        FakePasserelle* f = current();
        if (!f) {
            r.failure = app::ai::HttpResponse::Failure::Unavailable;
            r.error = "pas de faux GitHub";
            return r;
        }
        Answer a;
        {
            std::lock_guard<std::mutex> lock(f->mutex);
            if (f->offline) {
                f->requests.push_back(req.method + " " + req.url + " -> hors ligne");
                r.failure = app::ai::HttpResponse::Failure::Connect;
                r.error = "connexion impossible (essai : hors ligne)";
                return r;
            }
            a = f->handle(req);
            f->requests.push_back(req.method + " " + req.url + " -> " + std::to_string(a.status));
        }
        r.status = a.status;
        r.headers = a.headers;
        if (sink.onHeaders) sink.onHeaders(a.status, a.headers);
        if (sink.onData && !a.body.empty()) sink.onData(a.body);
        return r;
    }
    void cancel() override {}
    [[nodiscard]] bool cancelled() const noexcept override { return false; }
};

// Brancher le faux GitHub (et le debrancher : install(nullptr)).
inline void install(FakePasserelle* f) {
    current() = f;
    if (f) app::ai::setHttpFactory([] { return std::make_unique<Client>(); });
    else app::ai::setHttpFactory(nullptr);
}

} // namespace fakegh
