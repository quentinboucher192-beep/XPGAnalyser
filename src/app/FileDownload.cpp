// =============================================================================
//  app/FileDownload.cpp - 1.44.0 : telecharger un fichier du service, verifie
//  (voir FileDownload.hpp ; le contrat : docs/ADMINS.md, 6)
// =============================================================================
#include "FileDownload.hpp"

#include "LicenceService.hpp"
#include "ai/AiHttp.hpp"

#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Licensing.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

namespace app::filedl {

namespace {

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = licensing::admins;
namespace crypto = deploy::crypto;

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::mutex& rootMutex() {
    static std::mutex m;
    return m;
}
std::string& rootOverride() {
    static std::string r;
    return r;
}

// ============================================================ le transport ==
//  La requete en cours (le fil de fond, ou un appel bloquant) : cancel() et shutdown() l'arretent.
std::mutex& activeMutex() {
    static std::mutex m;
    return m;
}
ai::HttpClient*& activeClient() {
    static ai::HttpClient* c = nullptr;
    return c;
}

struct Got {
    ai::HttpResponse::Failure failure{ai::HttpResponse::Failure::None};
    std::string               error;
    int                       status{0};
    std::string               body;
    bool                      tooBig{false};
    [[nodiscard]] bool reached() const noexcept { return failure == ai::HttpResponse::Failure::None && !tooBig; }
};

// Une panne reseau (pas une reponse d'erreur) : l'adresse ne repond pas - les adresses de secours sont essayees.
bool unreachable(const Got& g) {
    using F = ai::HttpResponse::Failure;
    return g.failure == F::Resolve || g.failure == F::Connect || g.failure == F::Timeout || g.failure == F::Cut;
}

std::string hostOf(const std::string& url) {
    ai::Url u;
    if (ai::parseUrl(url, u)) return u.host;
    return "le service";
}

Got get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& extra, std::size_t maxBody, const std::atomic<bool>* stop) {
    Got g;
    if (stop && stop->load()) {
        g.failure = ai::HttpResponse::Failure::Cancelled;
        return g;
    }
    ai::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers = {{"User-Agent", std::string("XPG-Studio/") + XPG_ANALYZER_VERSION}, {"Cache-Control", "no-cache"}};
    for (const auto& h : extra) req.headers.push_back(h);
    req.connectTimeoutSeconds = 15;
    req.idleTimeoutSeconds = 45;
    auto client = ai::makeHttpClient();
    {
        std::lock_guard<std::mutex> lock(activeMutex());
        activeClient() = client.get();
    }
    if (stop && stop->load()) client->cancel();   // arrete entre-temps
    ai::HttpSink sink;
    sink.onData = [&g, maxBody](std::string_view chunk) {
        if (g.body.size() + chunk.size() > maxBody) g.tooBig = true;
        else g.body.append(chunk);
    };
    const ai::HttpResponse r = client->perform(req, sink);
    {
        std::lock_guard<std::mutex> lock(activeMutex());
        activeClient() = nullptr;
    }
    g.failure = r.failure;
    g.status = r.status;
    if (!r.ok()) g.error = r.error.empty() ? ai::failureText(r.failure, hostOf(url)) : r.error;
    return g;
}

void cancelActive() {
    std::lock_guard<std::mutex> lock(activeMutex());
    if (activeClient()) activeClient()->cancel();
}

// La phrase d'erreur du service ({"erreur","message"}) ; vide : aucune.
std::string serviceMessage(const Got& g) {
    const auto at = g.body.find("\"message\"");
    if (at == std::string::npos) return {};
    const auto q1 = g.body.find('"', g.body.find(':', at) + 1);
    if (q1 == std::string::npos) return {};
    std::string out;
    for (std::size_t i = q1 + 1; i < g.body.size() && out.size() < 220; ++i) {
        const char c = g.body[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < g.body.size()) {
            ++i;
            continue;
        }
        if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

// "Le service ne donne pas ce fichier : ..." (pas un fichier modifie : une panne, un acces refuse, un fichier parti).
std::string problemText(const Got& g, const std::string& name) {
    if (!g.reached()) {
        if (g.failure == ai::HttpResponse::Failure::Cancelled) return "Le t\xC3\xA9l\xC3\xA9" "chargement de " + name + " est arr\xC3\xAAt\xC3\xA9.";
        if (g.tooBig) return "Le service a r\xC3\xA9pondu trop d'octets pour " + name + " : rien n'est gard\xC3\xA9.";
        return "Le t\xC3\xA9l\xC3\xA9" "chargement de " + name + " s'est arr\xC3\xAAt\xC3\xA9 (" + g.error + ") : il reprendra l\xC3\xA0 o\xC3\xB9 il s'est arr\xC3\xAAt\xC3\xA9.";
    }
    const std::string said = serviceMessage(g);
    if (g.status == 404)
        return name + " n'est plus sur le service (un fichier joint est gard\xC3\xA9 30 jours, puis il faut le redemander).";
    if (g.status == 403)
        return "Le service refuse de donner " + name + (said.empty() ? std::string(".") : " : " + said);
    return "Le service n'a pas donn\xC3\xA9 " + name + " (erreur " + std::to_string(g.status) + ")" + (said.empty() ? std::string(".") : " : " + said);
}

std::string chunkUrl(const std::string& base, const Want& w, std::uint64_t n) {
    std::string u = base + "/v1/fichiers/morceau?id=" + w.id + "&n=" + std::to_string(n);
    if (!w.token.empty()) u += "&jeton=" + w.token;
    return u;
}

std::vector<std::pair<std::string, std::string>> accessHeaders(const Want& w) {
    std::vector<std::pair<std::string, std::string>> h;
    if (!w.adminToken.empty()) h.emplace_back("X-XPG-Jeton", w.adminToken);
    return h;
}

bool isHex(std::string_view s, std::size_t n) {
    return s.size() == n && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    const auto n = fs::file_size(P(path), ec);
    return ec ? 0 : static_cast<std::uint64_t>(n);
}

void removeQuietly(const std::string& path) {
    std::error_code ec;
    fs::remove(P(path), ec);
}

} // namespace

// ============================================================ les chemins, les textes ==
std::string rootFolder() {
    {
        std::lock_guard<std::mutex> lock(rootMutex());
        if (!rootOverride().empty()) return rootOverride();
    }
    std::error_code ec;
    fs::path t = fs::temp_directory_path(ec);
    if (ec || t.empty()) t = fs::path(".");
    return U(t / "XPG" / "fichiers");
}

void setRootForTests(std::string folder) {
    std::lock_guard<std::mutex> lock(rootMutex());
    rootOverride() = std::move(folder);
}

std::string finalPath(const Want& w) {
    if (!isHex(w.id, 32) || !adm::safeFileName(w.name)) return {};
    return U(P(rootFolder()) / w.id / P(w.name));
}

std::string refusedText(const std::string& name) {
    return name + " : la signature ne correspond pas : il a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 en route. Il n'est ni propos\xC3\xA9, ni enregistr\xC3\xA9.";
}

std::string sizeText(std::uint64_t bytes) {
    char b[32];
    if (bytes >= 10ull * 1024 * 1024) {
        std::snprintf(b, sizeof b, "%llu Mo", static_cast<unsigned long long>((bytes + 512ull * 1024) / (1024ull * 1024)));
        return b;
    }
    if (bytes >= 1024ull * 1024) {
        const unsigned long long tenths = (bytes * 10 + 512ull * 1024) / (1024ull * 1024);
        std::snprintf(b, sizeof b, "%llu,%llu Mo", tenths / 10, tenths % 10);
        return b;
    }
    if (bytes >= 1024) {
        std::snprintf(b, sizeof b, "%llu Ko", static_cast<unsigned long long>((bytes + 512) / 1024));
        return b;
    }
    return std::to_string(bytes) + " octets";
}

std::string shortHash(std::string_view sha) {
    if (sha.size() != 64) return std::string(sha);
    std::string up(sha);
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const auto group = [&up](std::size_t at) { return up.substr(at, 4); };
    return group(0) + " " + group(4) + " " + group(8) + " " + group(12) + " \xE2\x80\xA6 " + group(52) + " " + group(56) + " " + group(60);
}

// ============================================================ le manifeste ==
bool checkManifest(std::string_view blockText, const Want& w, adm::FileManifest& out, std::string& why) {
    std::vector<lic::SignedBlock> blocks;
    std::string r;
    if (!lic::readBlocks(blockText, blocks, &r) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::FileManifest) {
        why = "son manifeste est illisible";
        return false;
    }
    // LA CLE DE L'EDITEUR SEULEMENT (genre << fichier >> : jamais un admin)
    const lic::TrustedKey* signer = nullptr;
    const auto keys = lic::trustedPublisherKeys();
    for (const auto& k : keys)
        if (k.id == blocks[0].keyId) signer = &k;
    if (!signer) {
        why = "son manifeste n'est pas sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur";
        return false;
    }
    if (!lic::verifyBlock(blocks[0], signer->key)) {
        why = "la signature de son manifeste est fausse";
        return false;
    }
    adm::FileManifest m;
    if (!adm::parseFileManifest(blocks[0].document, m, &r)) {
        why = r;
        return false;
    }
    if (m.id != w.id || m.name != w.name || m.size != w.size || m.sha256 != w.sha256 || (!w.kind.empty() && m.kind != w.kind)) {
        why = "son manifeste ne dit pas ce qu'a annonc\xC3\xA9 l'\xC3\xA9" "diteur (nom, taille ou empreinte)";
        return false;
    }
    out = std::move(m);
    return true;
}

// ============================================================ la copie verifiee ==
bool presentCopy(const Want& w) {
    const std::string p = finalPath(w);
    return !p.empty() && fileSize(p) == w.size && w.size > 0;
}

std::optional<std::string> verifiedCopy(const Want& w) {
    const std::string p = finalPath(w);
    if (p.empty() || fileSize(p) != w.size || w.size == 0) return std::nullopt;
    std::string hex, why;
    std::uint64_t n = 0;
    if (!crypto::sha256File(p, hex, &n, &why)) return std::nullopt;
    if (hex != w.sha256 || n != w.size) {
        removeQuietly(p);   // change depuis sa verification : il ne sert plus
        return std::nullopt;
    }
    return p;
}

// ============================================================ le telechargement ==
Outcome download(const Want& w, const std::function<void(std::uint64_t, std::uint64_t)>& progress, const std::atomic<bool>* stop) {
    Outcome o;
    const std::string name = adm::safeFileName(w.name) ? w.name : std::string("le fichier");
    const auto refuse = [&](const std::string& partPath) {
        if (!partPath.empty()) removeQuietly(partPath);
        o.refused = true;
        o.message = refusedText(name);
        return o;
    };
    const std::string target = finalPath(w);
    if (target.empty() || w.size == 0 || w.size > adm::kMaxFileBytes || !isHex(w.sha256, 64)) return refuse({});
    if (!w.token.empty() && !isHex(w.token, 32)) return refuse({});
    // deja la et juste : rien a refaire
    if (auto p = verifiedCopy(w)) {
        o.ok = o.manifestVerified = o.hashVerified = true;
        o.path = *p;
        o.message = name + " est d\xC3\xA9j\xC3\xA0 t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9.";
        o.manifest.id = w.id;
        o.manifest.name = w.name;
        o.manifest.size = w.size;
        o.manifest.sha256 = w.sha256;
        if (progress) progress(w.size, w.size);
        return o;
    }
    // 1. le manifeste (l'adresse du service ; une panne reseau : les adresses de secours)
    std::vector<std::string> bases;
    if (const auto a = licence::service::address(); !a.empty()) bases.push_back(a.url);
    for (auto& f : licence::service::fallbackAddresses()) bases.push_back(std::move(f));
    if (bases.empty()) {
        o.message = "Ce programme n'a pas de service des licences : " + name + " ne peut pas \xC3\xAAtre t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9.";
        return o;
    }
    Got g;
    std::string base;
    for (const auto& b : bases) {
        g = get(b + "/v1/fichiers/manifeste?id=" + w.id, {}, 256u * 1024u, stop);
        base = b;
        if (!unreachable(g)) break;
    }
    if (!g.reached() || g.status != 200) {
        o.cancelled = g.failure == ai::HttpResponse::Failure::Cancelled;
        o.message = problemText(g, name);
        return o;
    }
    std::string why;
    if (!checkManifest(g.body, w, o.manifest, why)) {
        lic::HistoryEvent e;
        e.at = lic::systemNow();
        e.kind = "service";
        e.ok = false;
        e.text = "Fichier refus\xC3\xA9 : " + name + " (" + why + ").";
        (void)lic::addHistoryEvent(e, nullptr);
        return refuse({});
    }
    o.manifestVerified = true;
    const std::uint64_t chunk = o.manifest.chunk;
    const std::uint64_t count = o.manifest.chunks();
    // 2. les morceaux, repris apres le dernier entier
    std::error_code ec;
    fs::create_directories(P(target).parent_path(), ec);
    const std::string part = target + ".part";
    std::uint64_t have = fileSize(part);
    if (have > w.size) have = 0;
    have -= have % chunk;
    if (have == 0) removeQuietly(part);
    else fs::resize_file(P(part), have, ec);
    if (ec) {
        removeQuietly(part);
        have = 0;
    }
    if (progress) progress(have, w.size);
    for (std::uint64_t n = have / chunk; n < count; ++n) {
        const std::uint64_t expected = n + 1 < count ? chunk : w.size - (count - 1) * chunk;
        Got c = get(chunkUrl(base, w, n), accessHeaders(w), static_cast<std::size_t>(chunk) + 1, stop);
        if (!c.reached() || c.status != 200) {
            o.cancelled = c.failure == ai::HttpResponse::Failure::Cancelled;
            o.message = problemText(c, name);
            return o;
        }
        if (c.body.size() != expected) return refuse(part);   // un morceau de la mauvaise taille : modifie en route
        {
            std::ofstream out(P(part), std::ios::binary | std::ios::app);
            out.write(c.body.data(), static_cast<std::streamsize>(c.body.size()));
            out.flush();
            if (!out) {
                o.message = name + " n'a pas pu \xC3\xAAtre \xC3\xA9" "crit dans " + U(P(part).parent_path()) + " (le disque est-il plein ?).";
                return o;
            }
        }
        std::string().swap(c.body);   // jamais plus d'un morceau en memoire
        if (progress) progress(std::min<std::uint64_t>(w.size, (n + 1) * chunk), w.size);
    }
    // 3. le fichier entier : sa taille, son SHA-256
    std::string hex;
    std::uint64_t got = 0;
    if (!crypto::sha256File(part, hex, &got, &why) || got != w.size || hex != w.sha256) {
        lic::HistoryEvent e;
        e.at = lic::systemNow();
        e.kind = "service";
        e.ok = false;
        e.text = "Fichier refus\xC3\xA9 : " + name + " (son empreinte SHA-256 n'est pas celle du manifeste sign\xC3\xA9).";
        (void)lic::addHistoryEvent(e, nullptr);
        return refuse(part);
    }
    removeQuietly(target);
    fs::rename(P(part), P(target), ec);
    if (ec) {
        o.message = name + " est v\xC3\xA9rifi\xC3\xA9, mais il n'a pas pu \xC3\xAAtre rang\xC3\xA9 (" + ec.message() + ").";
        return o;
    }
    o.ok = o.hashVerified = true;
    o.path = target;
    o.message = name + " est t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9 (sign\xC3\xA9 par l'\xC3\xA9" "diteur, empreinte SHA-256 " + shortHash(hex) + ").";
    return o;
}

// ============================================================ le fil de fond ==
namespace {

struct Runner {
    std::mutex           mutex;
    std::thread          worker;
    std::atomic<bool>    running{false};
    std::atomic<bool>    stop{false};
    bool                 finished{false};
    std::vector<Outcome> results;
    std::string          tag;
    Progress             prog;

    ~Runner() {
        if (worker.joinable()) {
            stop = true;
            cancelActive();
            worker.join();
        }
    }
};

Runner& runner() {
    (void)activeMutex();   // construits avant : detruits apres
    (void)activeClient();
    (void)rootMutex();
    (void)rootOverride();
    static Runner r;
    return r;
}

} // namespace

bool start(std::vector<Want> files, std::string tag) {
    Runner& r = runner();
    if (r.running) return false;
    if (r.worker.joinable()) r.worker.join();
    r.stop = false;
    r.running = true;
    std::uint64_t total = 0;
    for (const auto& f : files) total += f.size;
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.finished = false;
        r.results.clear();
        r.tag = std::move(tag);
        r.prog = Progress{};
        r.prog.running = true;
        r.prog.files = files.size();
        r.prog.total = total;
        r.prog.name = files.empty() ? std::string() : files.front().name;
    }
    r.worker = std::thread([&r, files = std::move(files)] {
        std::vector<Outcome> out;
        std::uint64_t before = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
            {
                std::lock_guard<std::mutex> lock(r.mutex);
                r.prog.file = i;
                r.prog.name = files[i].name;
                r.prog.done = before;
            }
            Outcome o = download(files[i], [&r, before](std::uint64_t done, std::uint64_t) {
                std::lock_guard<std::mutex> lock(r.mutex);
                r.prog.done = before + done;
            }, &r.stop);
            const bool ok = o.ok;
            out.push_back(std::move(o));
            before += files[i].size;
            if (!ok) break;
        }
        std::lock_guard<std::mutex> lock(r.mutex);
        r.results = std::move(out);
        r.finished = true;
        r.prog.running = false;
        r.running = false;
    });
    return true;
}

bool busy() noexcept { return runner().running; }

Progress progress() {
    Runner& r = runner();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.prog;
}

void cancel() {
    Runner& r = runner();
    r.stop = true;
    cancelActive();
}

std::optional<std::vector<Outcome>> take(std::string* tag) {
    Runner& r = runner();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (!r.finished) return std::nullopt;
    r.finished = false;
    if (tag) *tag = r.tag;
    return std::move(r.results);
}

void shutdown() {
    Runner& r = runner();
    r.stop = true;
    cancelActive();
    if (r.worker.joinable()) r.worker.join();
    r.running = false;
}

} // namespace app::filedl
