// =============================================================================
//  tests/github_sync_test.cpp - 1.31.0 : la synchro des licences avec un depot
//  GitHub PRIVE (license_admin/GitHubSync), SANS RESEAU
// -----------------------------------------------------------------------------
//  Un FAUX GitHub dans ce processus (le transport injecte, app::ai::setHttpFactory)
//  : le depot, la branche, les objets Git (blobs, arbres, commits), la branche
//  avancee sans forcer, l'API Contents d'un depot vide, le jeton exige. Rien ne
//  part sur le reseau ; le vrai transport (libcurl, WinHTTP) est essaye a part,
//  contre outils/licences/faux_github.py.
//
//  Deux postes Admin (deux dossiers) : le premier publie (registre, licence,
//  registre des activations), le second recoit par le code de secours, signe a
//  son tour, publie ; le premier recoit ; plus rien a faire ensuite (pas d'aller-
//  retour sans fin). Puis : un envoi concurrent (la branche refuse, l'outil relit
//  et recommence), un mauvais jeton, un depot public, une autre cle de synchro,
//  un fichier chiffre modifie, un index modifie - chacun refuse, et dit pourquoi.
//  Dans le depot : AUCUN nom de client ni de PC en clair, jamais le jeton.
// =============================================================================
#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/GitHubSync.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/Store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = license_admin;
namespace gh = license_admin::github;
using core::json::Value;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// ---------------------------------------------------------------------------
//  le faux GitHub
// ---------------------------------------------------------------------------
struct FakeGitHub {
    std::string repo{"editeur/licences-xpg"};
    std::string token{"essai_jeton_github_00000000000000000000000"};
    std::string branch{"main"};
    bool        isPrivate{true};
    bool        raceOnce{false};
    std::map<std::string, std::string> blobs;                                  // sha -> contenu
    std::map<std::string, std::map<std::string, std::string>> trees;           // sha -> chemin -> sha du blob
    struct Commit {
        std::string tree;
        std::vector<std::string> parents;
        std::string message;
    };
    std::map<std::string, Commit> commits;
    std::map<std::string, std::string> refs;   // branche -> commit (vide : depot vide)
    std::vector<std::string> requests;         // "METHOD chemin -> statut"
    std::vector<std::string> bodies;           // les corps recus (pour verifier : jamais le jeton)
    int counter{0};

    std::string newSha(const std::string& kind, const std::string& content) {
        return deploy::crypto::sha256Hex(kind + "\n" + std::to_string(++counter) + "\n" + content).substr(0, 40);
    }
    std::string blobSha(const std::string& content) {
        const std::string sha = deploy::crypto::sha256Hex("blob\n" + content).substr(0, 40);
        blobs[sha] = content;
        return sha;
    }
    std::string makeTree(const std::map<std::string, std::string>& files) {
        std::string canon;
        for (const auto& [p, s] : files) canon += p + "=" + s + "\n";
        const std::string sha = deploy::crypto::sha256Hex("tree\n" + canon).substr(0, 40);
        trees[sha] = files;
        return sha;
    }
    std::string makeCommit(const std::string& tree, const std::vector<std::string>& parents, const std::string& message) {
        const std::string sha = newSha("commit", tree + message);
        commits[sha] = Commit{tree, parents, message};
        return sha;
    }
    // Un commit fait par quelqu'un d'autre (un autre poste, une main) : un fichier ajoute ou change.
    void commitDirect(const std::string& path, const std::string& content, const std::string& message) {
        const std::string head = refs[branch];
        auto files = trees[commits[head].tree];
        files[path] = blobSha(content);
        refs[branch] = makeCommit(makeTree(files), {head}, message);
    }
    [[nodiscard]] std::map<std::string, std::string> headFiles() {
        if (!refs.count(branch)) return {};
        std::map<std::string, std::string> out;
        for (const auto& [p, s] : trees[commits[refs[branch]].tree]) out[p] = blobs[s];
        return out;
    }
    [[nodiscard]] std::size_t commitCount() const { return commits.size(); }

    struct Answer {
        int status;
        std::string body;
    };
    static Answer json(int status, const Value& v) { return {status, core::json::write(v, false)}; }
    static Answer msg(int status, const std::string& m) {
        Value v = Value::object();
        v.set("message", m);
        return json(status, v);
    }

    Answer handle(const app::ai::HttpRequest& req) {
        std::string auth;
        for (const auto& [k, v] : req.headers)
            if (k == "Authorization") auth = v;
        bodies.push_back(req.body);
        if (auth != "Bearer " + token) return msg(401, "Bad credentials");
        const std::string base = "http://127.0.0.1:9/repos/";
        if (req.url.rfind(base, 0) != 0) return msg(404, "Not Found");
        std::string rest = req.url.substr(base.size());
        if (rest.rfind(repo, 0) != 0 || (rest.size() > repo.size() && rest[repo.size()] != '/')) return msg(404, "Not Found");
        std::string path = rest.substr(repo.size());
        std::string query;
        if (const auto q = path.find('?'); q != std::string::npos) {
            query = path.substr(q + 1);
            path.resize(q);
        }
        const bool empty = refs.empty();
        Value body;
        if (!req.body.empty()) (void)core::json::parse(req.body, body, nullptr);
        if (req.method == "GET" && path.empty()) {
            Value v = Value::object();
            v.set("full_name", repo);
            v.set("private", isPrivate);
            v.set("default_branch", branch);
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/ref/heads/", 0) == 0) {
            if (empty) return msg(409, "Git Repository is empty.");
            const auto it = refs.find(path.substr(15));
            if (it == refs.end()) return msg(404, "Not Found");
            Value v = Value::object(), o = Value::object();
            o.set("sha", it->second);
            o.set("type", std::string("commit"));
            v.set("ref", "refs/heads/" + it->first);
            v.set("object", std::move(o));
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/commits/", 0) == 0) {
            const auto it = commits.find(path.substr(13));
            if (it == commits.end()) return msg(404, "Not Found");
            Value v = Value::object(), t = Value::object(), a = Value::object();
            v.set("sha", it->first);
            t.set("sha", it->second.tree);
            v.set("tree", std::move(t));
            v.set("message", it->second.message);
            a.set("date", std::string("2026-11-06T22:00:00Z"));
            v.set("author", std::move(a));
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/trees/", 0) == 0) {
            const auto it = trees.find(path.substr(11));
            if (it == trees.end()) return msg(404, "Not Found");
            Value v = Value::object(), items = Value::array();
            v.set("sha", it->first);
            for (const auto& [p, s] : it->second) {
                Value e = Value::object();
                e.set("path", p);
                e.set("mode", std::string("100644"));
                e.set("type", std::string("blob"));
                e.set("sha", s);
                items.push(std::move(e));
            }
            v.set("tree", std::move(items));
            v.set("truncated", false);
            return json(200, v);
        }
        if (req.method == "GET" && path.rfind("/git/blobs/", 0) == 0) {
            const auto it = blobs.find(path.substr(11));
            if (it == blobs.end()) return msg(404, "Not Found");
            std::string b64 = app::ai::base64(it->second), wrapped;
            for (std::size_t i = 0; i < b64.size(); i += 60) wrapped += b64.substr(i, 60) + "\n";
            Value v = Value::object();
            v.set("sha", it->first);
            v.set("encoding", std::string("base64"));
            v.set("content", wrapped);
            return json(200, v);
        }
        if (empty && path.rfind("/git/", 0) == 0) return msg(409, "Git Repository is empty.");
        if (req.method == "POST" && path == "/git/blobs") {
            std::string content;
            const std::string b64 = body.str("content");
            // decodage base64 minimal (l'alphabet standard)
            auto val = [](char c) -> int {
                if (c >= 'A' && c <= 'Z') return c - 'A';
                if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52;
                if (c == '+') return 62;
                if (c == '/') return 63;
                return -1;
            };
            unsigned buf = 0;
            int bits = 0;
            for (char c : b64) {
                const int v = val(c);
                if (v < 0) continue;
                buf = (buf << 6) | static_cast<unsigned>(v);
                bits += 6;
                if (bits >= 8) {
                    content += static_cast<char>((buf >> (bits - 8)) & 0xFF);
                    bits -= 8;
                }
            }
            Value v = Value::object();
            v.set("sha", blobSha(content));
            return json(201, v);
        }
        if (req.method == "POST" && path == "/git/trees") {
            std::map<std::string, std::string> files;
            const std::string baseTree = body.str("base_tree");
            if (!baseTree.empty()) {
                if (!trees.count(baseTree)) return msg(422, "base_tree inconnu");
                files = trees[baseTree];
            }
            if (const Value* items = body.find("tree"))
                for (const auto& it : items->items()) {
                    if (!blobs.count(it.str("sha"))) return msg(422, "blob inconnu");
                    files[it.str("path")] = it.str("sha");
                }
            Value v = Value::object();
            v.set("sha", makeTree(files));
            return json(201, v);
        }
        if (req.method == "POST" && path == "/git/commits") {
            std::vector<std::string> parents;
            if (const Value* ps = body.find("parents"))
                for (const auto& p : ps->items()) parents.push_back(p.asString());
            if (!trees.count(body.str("tree"))) return msg(422, "arbre inconnu");
            Value v = Value::object();
            v.set("sha", makeCommit(body.str("tree"), parents, body.str("message")));
            return json(201, v);
        }
        if (req.method == "PATCH" && path.rfind("/git/refs/heads/", 0) == 0) {
            const std::string b = path.substr(16);
            if (!refs.count(b)) return msg(422, "Reference does not exist");
            if (raceOnce) {
                raceOnce = false;
                commitDirect("concurrent.txt", "un autre poste\n", "commit concurrent");
            }
            const std::string target = body.str("sha");
            if (!commits.count(target)) return msg(422, "commit inconnu");
            // avance rapide : la tete actuelle doit etre un ancetre de la cible
            bool ff = false;
            std::vector<std::string> todo{target};
            while (!todo.empty() && !ff) {
                const std::string c = todo.back();
                todo.pop_back();
                if (c == refs[b]) ff = true;
                for (const auto& p : commits[c].parents) todo.push_back(p);
            }
            if (!ff && !body.flag("force")) return msg(422, "Update is not a fast forward");
            refs[b] = target;
            Value v = Value::object(), o = Value::object();
            o.set("sha", target);
            v.set("object", std::move(o));
            return json(200, v);
        }
        if (req.method == "PUT" && path.rfind("/contents/", 0) == 0) {
            if (!empty) return msg(422, "seulement pour un depot vide dans ce faux");
            std::string content = body.str("content");
            Value fake = Value::object();
            fake.set("content", content);
            // reutilise le decodeur des blobs
            app::ai::HttpRequest blobReq;
            blobReq.method = "POST";
            blobReq.url = "http://127.0.0.1:9/repos/" + repo + "/git/blobs";
            blobReq.headers = {{"Authorization", "Bearer " + token}};
            blobReq.body = core::json::write(fake, false);
            refs[body.str("branch", branch)] = "";   // le depot n'est plus vide
            const Answer a = handle(blobReq);
            Value got;
            (void)core::json::parse(a.body, got, nullptr);
            std::map<std::string, std::string> files{{path.substr(10), got.str("sha")}};
            refs[body.str("branch", branch)] = makeCommit(makeTree(files), {}, body.str("message"));
            Value v = Value::object(), c = Value::object();
            c.set("sha", refs[body.str("branch", branch)]);
            v.set("commit", std::move(c));
            return json(201, v);
        }
        return msg(404, "Not Found");
    }
};

FakeGitHub* g_fake = nullptr;

class FakeClient final : public app::ai::HttpClient {
public:
    app::ai::HttpResponse perform(const app::ai::HttpRequest& req, const app::ai::HttpSink& sink) override {
        app::ai::HttpResponse r;
        const auto a = g_fake->handle(req);
        std::string shortPath = req.url;
        g_fake->requests.push_back(req.method + " " + shortPath + " -> " + std::to_string(a.status));
        r.status = a.status;
        if (sink.onHeaders) sink.onHeaders(a.status, {});
        if (sink.onData) sink.onData(a.body);
        return r;
    }
    void cancel() override {}
    [[nodiscard]] bool cancelled() const noexcept override { return false; }
};

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

gh::SyncReport sync(const fs::path& pc, const lic::SigningKey* key, bool receiveOnly = false, bool dryRun = false) {
    gh::SyncOptions o;
    o.receiveOnly = receiveOnly;
    o.dryRun = dryRun;
    o.now = lic::systemNow();
    o.tool = "XPG-License-Admin essai";
    return gh::synchronize(U(pc), key, o);
}

void setup(const fs::path& pc, const std::string& token, std::string* why) {
    gh::Config c;
    c.repo = g_fake->repo;
    c.api = "http://127.0.0.1:9";
    (void)gh::writeConfig(U(pc), c, why);
    (void)gh::saveToken(U(pc), token, why);
}

bool anyBlobHas(const std::string& needle) {
    for (const auto& [sha, content] : g_fake->blobs)
        if (has(content, needle)) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path base = fs::temp_directory_path() / ("xpg-github-sync-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(base, ec);
    FakeGitHub fake;
    g_fake = &fake;
    app::ai::setHttpFactory([] { return std::make_unique<FakeClient>(); });
    std::string why;

    // ---- les briques : code de secours, chiffre, lignes manquantes ----
    std::printf("-- les briques\n");
    {
        gh::SyncKey k{}, back{};
        deploy::crypto::randomBytes(k.data(), k.size());
        const std::string code = gh::recoveryCode(k);
        check(code.rfind("XPGS1-", 0) == 0 && code.size() == 6 + 11 * 5 + 10, "le code de secours : XPGS1- et 11 groupes de 5 (" + code.substr(0, 12) + "...)");
        check(gh::parseRecoveryCode(code, back, &why) && back == k, "le code redonne la cle (" + why + ")");
        std::string lowered = code;
        for (auto& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto& c : lowered)
            if (c == '0') c = 'o';
        check(gh::parseRecoveryCode(lowered, back, &why) && back == k, "minuscules et O pour 0 : toleres");
        std::string typo = code;
        typo[8] = typo[8] == 'A' ? 'B' : 'A';
        check(!gh::parseRecoveryCode(typo, back, &why) && has(why, "faux"), "une faute de frappe : refusee (" + why + ")");
        check(gh::syncKeyId(k).rfind("xpgs-", 0) == 0 && gh::syncKeyId(k).size() == 21 && !has(code, gh::syncKeyId(k).substr(5)),
              "l'identifiant de la cle : xpgs- et 16 hexadecimaux");
        const std::string plain = "{\"customer\": \"Client secret\"}\n";
        const std::string sealed = gh::seal(k, "licences/A.xpglic.chiffre", plain);
        std::string out;
        check(!has(sealed, "Client secret") && gh::openSealed(k, "licences/A.xpglic.chiffre", sealed, out, &why) && out == plain,
              "chiffrer, dechiffrer : le meme texte, rien en clair");
        check(!gh::openSealed(k, "licences/B.xpglic.chiffre", sealed, out, &why), "un fichier chiffre deplace ne se dechiffre plus (" + why + ")");
        gh::SyncKey other{};
        deploy::crypto::randomBytes(other.data(), other.size());
        check(!gh::openSealed(other, "licences/A.xpglic.chiffre", sealed, out, &why) && has(why, "autre cl"), "une autre cle : refusee (" + why + ")");
        std::string damaged = sealed;
        damaged[damaged.size() / 2] = damaged[damaged.size() / 2] == 'A' ? 'B' : 'A';
        check(!gh::openSealed(k, "licences/A.xpglic.chiffre", damaged, out, &why), "un chiffre abime : refuse");
        const auto miss = gh::missingLines("a\nb\n", "b\nc\r\nc\nd\n");
        check(miss.size() == 2 && miss[0] == "c" && miss[1] == "d", "les lignes manquantes : c et d (sans double)");
        check(gh::validRepo("editeur/licences-xpg") && !gh::validRepo("editeur") && !gh::validRepo("a/b/c") && !gh::validRepo("x/..")
                  && gh::validBranch("main") && !gh::validBranch("a..b") && gh::validRoot("licences/xpg") && !gh::validRoot("../x"),
              "les noms : depot, branche, dossier");
        check(gh::safeName("XPG-2026/00042 ..") == "XPG-2026_00042_..", "le nom de fichier d'une licence : " + gh::safeName("XPG-2026/00042 .."));
    }

    // ---- les deux postes : la cle de l'editeur, le registre, deux PC, une licence ----
    const fs::path pc1 = base / "admin-1", pc2 = base / "admin-2";
    auto key = adm::createPublisherKey(U(pc1), "essai github_sync_test", true, &why);
    check(key != nullptr, "la cle d'essai de l'editeur (" + why + ")");
    if (!key) return 1;
    const auto now = lic::systemNow();
    std::string reg;
    check(adm::signRegistry(*key, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), now, reg, &why), "le registre signe (" + why + ")");
    const auto request = [&](const std::string& folder, const std::string& name, const std::string& id) {
        lic::StoreOptions o;
        o.folder = U(base / folder);
        o.trustedKeys = {trustedOf(*key)};
        o.compiledKeys = false;
        (void)lic::initialize(o);
        const fs::path p = base / (folder + ".xpgreq");
        (void)lic::writeActivationRequest(U(p), name, id, "IHM Studio (essai)", &why);
        return readFile(p);
    };
    const std::string reqA = request("pc-client-a", "Poste Atelier Nord", "XPG-GH-0001");
    const std::string reqB = request("pc-client-b", "Poste Bureau Sud", "XPG-GH-0002");
    check(!reqA.empty() && !reqB.empty(), "deux demandes d'activation (deux PC)");
    const auto signLicence = [&](const std::string& id, const std::string& customer, const std::string& req, std::string& out,
                                 std::vector<adm::ActivationRecord>& records) {
        adm::LicenseInputs in;
        in.params = "{\"license_id\": \"" + id + "\", \"customer\": \"" + customer + "\", \"edition\": \"PROFESSIONAL\", \"max_devices\": 2, \"duration_days\": 365}";
        in.requests = {req};
        in.registry = reg;
        std::string summary;
        return adm::signLicense(*key, in, now, out, &why, &summary, &records);
    };
    std::string licA;
    std::vector<adm::ActivationRecord> recA;
    check(signLicence("XPG-GH-0001", "Fonderie Imaginaire", reqA, licA, recA), "la licence A signee (" + why + ")");
    const auto trusted = gh::trustedKeys(key.get());
    std::string what;
    check(gh::archiveSigned(U(pc1), reg, trusted, &what, &why) && has(what, "archive"), "le registre range dans l'archive (" + what + ")");
    check(gh::archiveSigned(U(pc1), licA, trusted, &what, &why), "la licence A rangee (" + what + ")");
    check(gh::archiveSigned(U(pc1), reg, trusted, &what, &why) && has(what, "d\xC3\xA9j\xC3\xA0"), "le meme registre : deja la (" + what + ")");
    check(!gh::archiveSigned(U(pc1), reqA, trusted, &what, &why) && has(why, "demande"), "une demande ne se range pas (" + why + ")");
    check(!gh::archiveSigned(U(pc1), reg, {}, &what, &why), "sans cle de confiance : refuse");
    check(adm::appendActivations(U(pc1), recA, &why), "le registre des activations du poste 1");

    // ---- sans configuration ----
    std::printf("-- sans configuration\n");
    {
        const auto r = sync(pc1, key.get());
        check(!r.ok && has(r.why, "github-config"), "pas configuree : refusee, et dit quoi faire (" + r.why + ")");
    }

    // ---- poste 1 : premier envoi dans un depot vide ----
    std::printf("-- poste 1 : le premier envoi\n");
    gh::SyncKey k1{};
    setup(pc1, fake.token, &why);
    check(gh::createSyncKey(U(pc1), k1, &why), "la cle de synchro du poste 1 (" + why + ")");
    {
        gh::SyncKey again{};
        check(!gh::createSyncKey(U(pc1), again, &why) && has(why, "jamais remplac"), "une seconde cle de synchro : refusee");
        std::string tok;
        check(gh::loadToken(U(pc1), tok, &why) && tok == fake.token, "le jeton se relit");
        check(!has(readFile(pc1 / gh::kTokenFile), fake.token), "le fichier du jeton ne le contient pas en clair");
    }
    {
        const auto preview = sync(pc1, key.get(), false, true);
        check(preview.ok && preview.preview && preview.sent.size() == 3 && fake.commitCount() == 0, "github-etat : 3 fichiers a envoyer, rien d'ecrit\n" + preview.text());
        const auto r = sync(pc1, key.get());
        check(r.ok && r.initialized && !r.commit.empty() && r.sent.size() == 3, "le premier envoi : depot initialise, un commit, 3 fichiers\n" + r.text());
        const auto files = fake.headFiles();
        check(files.count("LISEZ-MOI.md") && files.count("index.xpgsync") && files.count("registre/registre.xpgreg")
                  && files.count("licences/XPG-GH-0001.xpglic.chiffre") && files.count("activations/activations.jsonl.chiffre"),
              "dans le depot : LISEZ-MOI, index, registre, licence chiffree, activations chiffrees");
        check(files.count("registre/registre.xpgreg") && files.at("registre/registre.xpgreg") == reg, "le registre est publie tel quel (signe, en clair)");
        check(!anyBlobHas("Fonderie Imaginaire") && !anyBlobHas("Poste Atelier Nord") && !anyBlobHas("XPG-GH-0001\", \"customer"),
              "aucun nom de client ni de PC en clair dans le depot");
        bool tokenInBody = false;
        for (const auto& b : fake.bodies) tokenInBody = tokenInBody || has(b, fake.token);
        check(!anyBlobHas(fake.token) && !tokenInBody, "le jeton n'est dans aucun fichier ni aucun corps envoye");
        bool namesInMessages = false;
        for (const auto& [sha, c] : fake.commits) namesInMessages = namesInMessages || has(c.message, "Fonderie") || has(c.message, "Atelier");
        check(!namesInMessages && has(fake.commits[fake.refs["main"]].message, "XPG-GH-0001"), "le message du commit : l'identifiant, jamais le client");
        check(fake.commitCount() == 2, "deux commits : le LISEZ-MOI, puis l'envoi (" + std::to_string(fake.commitCount()) + ")");
        const auto again = sync(pc1, key.get());
        check(again.ok && again.upToDate && again.commit.empty() && fake.commitCount() == 2, "resynchroniser : deja a jour, aucun commit\n" + again.text());
    }

    // ---- poste 2 : le code de secours, puis recevoir ----
    std::printf("-- poste 2 : recevoir par le code de secours\n");
    {
        setup(pc2, fake.token, &why);
        gh::SyncKey k2{};
        std::string bad = gh::recoveryCode(k1);
        bad[10] = bad[10] == 'Z' ? 'Y' : 'Z';
        check(!gh::restoreSyncKey(U(pc2), bad, k2, &why), "un code faux : refuse (" + why + ")");
        check(gh::restoreSyncKey(U(pc2), gh::recoveryCode(k1), k2, &why) && k2 == k1, "le code redonne la cle sur le poste 2 (" + why + ")");
        check(gh::restoreSyncKey(U(pc2), gh::recoveryCode(k1), k2, &why), "le meme code une seconde fois : rien ne change");
        const auto r = sync(pc2, nullptr, true);
        check(r.ok && r.received.size() == 3 && r.commit.empty(), "recevoir seulement, sans cle d'editeur : 3 fichiers, aucun commit\n" + r.text());
        check(readFile(pc2 / "archive" / "licences" / "XPG-GH-0001.xpglic") == licA && readFile(pc2 / "archive" / "registre" / "registre.xpgreg") == reg,
              "l'archive du poste 2 : la licence et le registre, octet pour octet");
        std::vector<adm::ActivationRecord> rec;
        int skipped = 0;
        check(adm::readActivations(U(pc2), rec, &skipped, &why) && rec.size() == recA.size() && skipped == 0, "le registre des activations recu");
        const auto devices = adm::knownDevices(rec, lic::systemNow());
        check(devices.size() == 1 && devices[0].customer == "Fonderie Imaginaire" && devices[0].state == "active", "liste sur le poste 2 : le PC du client A, actif");
    }

    // ---- poste 2 signe a son tour, publie ; le poste 1 recoit ----
    std::printf("-- poste 2 publie, poste 1 recoit\n");
    std::string licB;
    {
        fs::copy_file(pc1 / adm::kPublisherKeyFile, pc2 / adm::kPublisherKeyFile, fs::copy_options::overwrite_existing, ec);
        auto key2 = adm::loadPublisherKey(U(pc2), &why);
        check(key2 != nullptr, "la cle de l'editeur sur le poste 2 (essai : sous Linux, un fichier) (" + why + ")");
        std::vector<adm::ActivationRecord> recB;
        check(signLicence("XPG-GH-0002", "Verrerie Fictive", reqB, licB, recB), "la licence B signee sur le poste 2");
        check(gh::archiveSigned(U(pc2), licB, gh::trustedKeys(key2.get()), &what, &why) && adm::appendActivations(U(pc2), recB, &why),
              "rangee, et le registre des activations du poste 2");
        const auto r = sync(pc2, key2.get());
        check(r.ok && !r.commit.empty() && r.sent.size() == 2, "le poste 2 publie : la licence B et le registre des activations\n" + r.text());
        check(!anyBlobHas("Verrerie Fictive") && !anyBlobHas("Poste Bureau Sud"), "toujours aucun nom en clair");
        const auto r1 = sync(pc1, key.get());
        check(r1.ok && r1.received.size() == 2 && r1.commit.empty(), "le poste 1 recoit : la licence B, les lignes du poste 2, sans rien renvoyer\n" + r1.text());
        std::vector<adm::ActivationRecord> rec;
        int skipped = 0;
        (void)adm::readActivations(U(pc1), rec, &skipped, &why);
        const auto devices = adm::knownDevices(rec, lic::systemNow());
        check(devices.size() == 2, "liste sur le poste 1 : les deux PC (" + std::to_string(devices.size()) + ")");
        const std::size_t commits = fake.commitCount();
        const auto again1 = sync(pc1, key.get());
        const auto again2 = sync(pc2, key2.get());
        check(again1.upToDate && again2.upToDate && fake.commitCount() == commits, "puis plus rien : ni d'un cote ni de l'autre (pas d'aller-retour)\n" + again1.text() + again2.text());
    }

    // ---- un envoi concurrent ----
    std::printf("-- un autre poste publie pendant l'envoi\n");
    {
        std::string rev;
        std::vector<adm::ActivationRecord> recR;
        check(adm::signRevocations(*key, "{\"number\": 1, \"revoked\": [{\"license_id\": \"XPG-GH-0002\", \"reason\": \"Essai\"}]}", now, rev, &why, &recR),
              "une liste de revocation signee");
        check(gh::archiveSigned(U(pc1), rev, trusted, &what, &why) && adm::appendActivations(U(pc1), recR, &why), "rangee");
        fake.raceOnce = true;
        const auto r = sync(pc1, key.get());
        bool warned = false;
        for (const auto& w : r.warnings) warned = warned || has(w, "autre poste");
        check(r.ok && r.attempts == 2 && warned && !r.commit.empty(), "la branche refuse une fois : l'outil relit, recommence, et le dit\n" + r.text());
        const auto files = fake.headFiles();
        check(files.count("concurrent.txt") && files.count("revocations/revocations.xpgrev"), "le fichier de l'autre poste est garde, la revocation publiee");
        const auto r2 = sync(pc2, nullptr, true);
        std::vector<adm::ActivationRecord> rec;
        int skipped = 0;
        (void)adm::readActivations(U(pc2), rec, &skipped, &why);
        bool revokedB = false;
        for (const auto& d : adm::knownDevices(rec, lic::systemNow())) revokedB = revokedB || (d.licenseId == "XPG-GH-0002" && d.state == "revoque");
        check(r2.ok && revokedB, "le poste 2 recoit la revocation : le PC de la licence B est revoque");
    }

    // ---- les refus ----
    std::printf("-- les refus\n");
    {
        const fs::path pc3 = base / "admin-3";
        const std::string wrong = "essai_jeton_faux_111111111111111111111111";
        setup(pc3, wrong, &why);
        gh::SyncKey k3{};
        (void)gh::restoreSyncKey(U(pc3), gh::recoveryCode(k1), k3, &why);
        auto r = sync(pc3, nullptr);
        check(!r.ok && has(r.why, "jeton") && !has(r.why, wrong) && !has(r.text(), wrong), "un mauvais jeton : refuse, le dit, ne l'ecrit pas\n" + r.why);
        (void)gh::saveToken(U(pc3), fake.token, &why);
        fake.isPrivate = false;
        r = sync(pc3, nullptr);
        check(!r.ok && has(r.why, "PUBLIC"), "un depot public : rien n'y est publie (" + r.why + ")");
        fake.isPrivate = true;
        const fs::path pc4 = base / "admin-4";
        setup(pc4, fake.token, &why);
        gh::SyncKey k4{};
        (void)gh::createSyncKey(U(pc4), k4, &why);
        const std::size_t commits = fake.commitCount();
        r = sync(pc4, nullptr);
        check(!r.ok && has(r.why, "AUTRE cl") && fake.commitCount() == commits, "une autre cle de synchro : refusee, rien n'est ecrit (" + r.why + ")");

        // Un fichier chiffre modifie dans le depot : un poste neuf ne le recoit pas, recoit le reste.
        auto files = fake.headFiles();
        std::string sealed = files["licences/XPG-GH-0001.xpglic.chiffre"];
        const auto nl = sealed.find('\n');
        sealed[nl + 10] = sealed[nl + 10] == 'A' ? 'B' : 'A';
        fake.commitDirect("licences/XPG-GH-0001.xpglic.chiffre", sealed, "une main");
        const fs::path pc5 = base / "admin-5";
        setup(pc5, fake.token, &why);
        gh::SyncKey k5{};
        (void)gh::restoreSyncKey(U(pc5), gh::recoveryCode(k1), k5, &why);
        r = sync(pc5, nullptr, true);
        bool refusedA = false;
        for (const auto& w : r.warnings) refusedA = refusedA || has(w, "XPG-GH-0001");
        check(r.ok && refusedA && !fs::exists(pc5 / "archive" / "licences" / "XPG-GH-0001.xpglic") && fs::exists(pc5 / "archive" / "licences" / "XPG-GH-0002.xpglic"),
              "une licence chiffree modifiee : refusee ; les autres fichiers recus\n" + r.text());
        // Le poste 1 la republie (il a la bonne) : le depot est repare.
        r = sync(pc1, key.get());
        check(r.ok && !r.commit.empty(), "le poste 1 republie la bonne licence : le depot est repare\n" + r.text());
        r = sync(pc5, nullptr, true);
        check(r.ok && readFile(pc5 / "archive" / "licences" / "XPG-GH-0001.xpglic") == licA, "le poste neuf la recoit alors");

        // Un index modifie : tout est refuse.
        files = fake.headFiles();
        std::string index = files["index.xpgsync"];
        const auto at = index.find("XPG-GH-0001");
        index.replace(at, 11, "XPG-GH-9999");
        fake.commitDirect("index.xpgsync", index, "une main");
        r = sync(pc1, key.get());
        check(!r.ok && has(r.why, "index"), "un index modifie hors de l'outil : rien n'est recu ni envoye (" + r.why + ")");
    }

    // ---- l'ordre des lignes ne change pas la liste ----
    std::printf("-- l'ordre des lignes\n");
    {
        std::vector<adm::ActivationRecord> rec;
        int skipped = 0;
        (void)adm::readActivations(U(pc1), rec, &skipped, &why);
        // Des heures distinctes (dans l'essai, tout tient dans la meme seconde ; a la meme heure, l'ordre du fichier compte).
        for (std::size_t i = 0; i < rec.size(); ++i) rec[i].at = 1'800'000'000 + static_cast<lic::Seconds>(i) * 60;
        auto reversed = rec;
        std::reverse(reversed.begin(), reversed.end());
        const auto a = adm::knownDevices(rec, lic::systemNow()), b = adm::knownDevices(reversed, lic::systemNow());
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) same = a[i].deviceKey == b[i].deviceKey && a[i].state == b[i].state;
        check(same && !rec.empty(), "liste : le meme etat de chaque PC, quel que soit l'ordre des lignes");
    }

    app::ai::setHttpFactory(nullptr);
    lic::resetForTests();
    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
