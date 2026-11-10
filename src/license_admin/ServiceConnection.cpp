// =============================================================================
//  license_admin/ServiceConnection.cpp - 1.47.0 : la connexion au service qui
//  voyage avec l'installateur de l'Admin (ServiceConnection.hpp)
// =============================================================================
#include "ServiceConnection.hpp"

#include "ServiceClient.hpp"

#include "../core/Json.hpp"
#include "../deploy/Crypto.hpp"
#include "../deploy/Paths.hpp"
#include "../licensing/Service.hpp"
#include "../licensing/Store.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace license_admin::connexion {

namespace fs = std::filesystem;
namespace crypto = deploy::crypto;
namespace svc = licensing::service;
using core::json::Value;
using licensing::DocKind;
using licensing::SignedBlock;

namespace {

fs::path P(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
bool isFile(const std::string& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(P(p), ec);
}
bool readText(const std::string& path, std::string& out, std::string* why) {
    crypto::Bytes b;
    if (!crypto::readFile(path, b, why, 256u * 1024u)) return false;
    out.assign(b.begin(), b.end());
    return true;
}
bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}
std::string at(const std::string& folder, const char* name) { return deploy::joinPath(folder, name); }

std::mutex& placesMutex() {
    static std::mutex m;
    return m;
}
std::optional<Places>& placesSlot() {
    static std::optional<Places> p;
    return p;
}

Value refusedJson(const std::vector<Refused>& refused) {
    Value arr = Value::array();
    for (const auto& r : refused) {
        Value o = Value::object();
        o.set("fichier", r.path);
        o.set("source", r.source);
        o.set("pourquoi", r.why);
        arr.push(std::move(o));
        if (arr.items().size() >= 8) break;
    }
    return arr;
}

Value readStateJson(const std::string& folder) {
    std::string text;
    Value v;
    if (!isFile(at(folder, kStateFile)) || !readText(at(folder, kStateFile), text, nullptr) || !core::json::parse(text, v, nullptr) || !v.isObject() ||
        v.str("format") != "xpg-connexion-service/1")
        return Value::object();
    return v;
}

bool writeStateJson(const std::string& folder, const Value& v, std::string* why) {
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    const std::string text = core::json::write(v, true) + "\n";
    return crypto::replaceFile(at(folder, kStateFile), text, false, why);
}

} // namespace

// ------------------------------------------------------------------------------------------------------------- lire
bool read(std::string_view text, const std::vector<licensing::crypto::PublicKey>& editors, Seconds now, Connection& out, std::string* why) {
    std::vector<SignedBlock> blocks;
    std::string w;
    if (!licensing::readBlocks(text, blocks, &w))
        return fail(why, "ce n'est pas une connexion sign\xC3\xA9" "e (" + (w.empty() ? std::string("illisible") : w) + ")");
    const SignedBlock* cert = nullptr;
    for (const auto& b : blocks)
        if (b.kind == DocKind::ServiceCert) {
            cert = &b;
            break;
        }
    if (!cert) return fail(why, "ce n'est pas une connexion sign\xC3\xA9" "e : aucun certificat du service dedans");
    const licensing::crypto::PublicKey* signer = nullptr;
    bool known = false;
    for (const auto& k : editors)
        if (licensing::keyIdOf(k) == cert->keyId) {
            known = true;
            if (licensing::verifyBlock(*cert, k)) signer = &k;
        }
    if (known && !signer) return fail(why, "la signature ne correspond pas : le fichier a \xC3\xA9t\xC3\xA9 modifi\xC3\xA9 (l'adresse ou la cl\xC3\xA9 du service), il est refus\xC3\xA9");
    if (!signer)
        return fail(why, "sign\xC3\xA9" "e par une cl\xC3\xA9 (" + cert->keyId + ") qui n'est pas celle de l'\xC3\xA9" "diteur que cet Admin conna\xC3\xAEt : refus\xC3\xA9" "e");
    svc::ServiceCert c;
    if (!svc::parseCert(cert->document, c, &w)) return fail(why, "le certificat du service est illisible (" + w + ")");
    const std::string url = svc::normalizeUrl(c.url);
    if (url.empty()) return fail(why, "l'adresse du certificat est refus\xC3\xA9" "e (" + c.url + ")");
    if (!svc::isKeyHex(c.key)) return fail(why, "la cl\xC3\xA9 du service du certificat est illisible");
    Connection r;
    r.url = url;
    r.serviceKey = c.key;
    r.editorKeyId = cert->keyId;
    r.issuedAt = c.issuedAt;
    r.expiresAt = c.expiresAt;
    r.expired = now > 0 && now > c.expiresAt;
    r.block = licensing::writeBlock(*cert);
    out = std::move(r);
    return true;
}

// ------------------------------------------------------------------------------------------------------------- les lieux
Places defaultPlaces(const std::string& exeDir, const std::string& explicitFile) {
    Places p;
    p.explicitFile = explicitFile;
    if (!exeDir.empty()) p.beside.push_back(exeDir);
#ifdef _WIN32
    p.studioFolder = licensing::LicenseStore::defaultFolder();
#endif
    return p;
}

void setPlaces(Places p) {
    std::lock_guard<std::mutex> lock(placesMutex());
    placesSlot() = std::move(p);
}

const Places* places() {
    std::lock_guard<std::mutex> lock(placesMutex());
    return placesSlot() ? &*placesSlot() : nullptr;
}

Discovery discover(const Places& p, const std::vector<licensing::crypto::PublicKey>& editors, Seconds now) {
    Discovery d;
    // `required` : un fichier donne pour etre une connexion (sans certificat : refuse) ; sinon (le Studio) : passe.
    const auto tryFile = [&](const std::string& path, const std::string& source, bool required) {
        if (d.found || !isFile(path)) return;
        d.looked.push_back(path);
        std::string text, why;
        Connection c;
        if (!readText(path, text, &why)) {
            if (required) d.refused.push_back({path, source, why});
            return;
        }
        if (!read(text, editors, now, c, &why)) {
            // un fichier du Studio sans certificat (pas encore parle au service) : ce n'est pas un refus
            if (required || why.find("aucun certificat") == std::string::npos) d.refused.push_back({path, source, why});
            return;
        }
        c.source = source;
        c.path = path;
        d.connection = std::move(c);
        d.found = true;
    };
    if (!p.explicitFile.empty()) {
        if (!isFile(p.explicitFile)) d.refused.push_back({p.explicitFile, "--connexion", "le fichier n'existe pas"});
        tryFile(p.explicitFile, "le fichier donn\xC3\xA9 (--connexion)", true);
    }
    for (const auto& dir : p.beside) {
        if (d.found) break;
        std::vector<std::string> files;
        std::error_code ec;
        for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = U(it->path().filename());
            // connexion-service.xpgsvc, et les copies renommees par un navigateur : connexion-service (1).xpgsvc
            if (name.rfind("connexion-service", 0) == 0 && name.size() > 7 && name.substr(name.size() - 7) == ".xpgsvc") files.push_back(U(it->path()));
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) tryFile(f, "\xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme (" + U(P(f).filename()) + ", copi\xC3\xA9 par l'installateur)", true);
    }
    if (!d.found && !p.studioFolder.empty())
        for (const char* name : {licensing::LicenseStore::kActivationFile, licensing::LicenseStore::kTrialFile, licensing::LicenseStore::kLeaseFile})
            tryFile(at(p.studioFolder, name), std::string("le Studio de ce PC (") + name + ")", false);
    return d;
}

// ------------------------------------------------------------------------------------------------------------- adopter
bool adopt(const std::string& folder, const Connection& c, bool replace, Seconds now, std::string* why) {
    if (online::configured(folder) && !replace) return fail(why, "une adresse du service est d\xC3\xA9j\xC3\xA0 r\xC3\xA9gl\xC3\xA9" "e sur ce poste : elle n'est pas remplac\xC3\xA9" "e");
    std::error_code ec;
    fs::create_directories(P(folder), ec);
    online::Config cfg;
    cfg.url = c.url;
    if (!online::writeConfig(folder, cfg, why)) return false;
    Value v = Value::object();
    v.set("format", "xpg-connexion-service/1");
    v.set("url", c.url);
    v.set("cle_service", c.serviceKey);
    v.set("cle_editeur", c.editorKeyId);
    v.set("emis", static_cast<std::int64_t>(c.issuedAt));
    v.set("expire", static_cast<std::int64_t>(c.expiresAt));
    v.set("source", c.source);
    v.set("fichier", c.path);
    v.set("adoptee", static_cast<std::int64_t>(now));
    v.set("cherchee", static_cast<std::int64_t>(now));
    v.set("refus", Value::array());
    if (!writeStateJson(folder, v, why)) return false;
    (void)crypto::replaceFile(at(folder, kFileName), c.block, false, nullptr);
    return true;
}

void noteRefused(const std::string& folder, const std::vector<Refused>& refused, Seconds now) {
    Value v = readStateJson(folder);
    v.set("format", "xpg-connexion-service/1");
    v.set("cherchee", static_cast<std::int64_t>(now));
    v.set("refus", refusedJson(refused));
    (void)writeStateJson(folder, v, nullptr);
}

bool readState(const std::string& folder, Connection* adopted, std::vector<Refused>* refused, Seconds* lookedAt) {
    const Value v = readStateJson(folder);
    if (refused) {
        refused->clear();
        if (const Value* a = v.find("refus"); a && a->isArray())
            for (const auto& r : a->items())
                if (r.isObject()) refused->push_back({r.str("fichier"), r.str("source"), r.str("pourquoi")});
    }
    if (lookedAt) *lookedAt = static_cast<Seconds>(v.integer("cherchee"));
    if (v.str("url").empty()) return false;
    if (adopted) {
        Connection c;
        c.url = v.str("url");
        c.serviceKey = v.str("cle_service");
        c.editorKeyId = v.str("cle_editeur");
        c.issuedAt = static_cast<Seconds>(v.integer("emis"));
        c.expiresAt = static_cast<Seconds>(v.integer("expire"));
        c.source = v.str("source");
        c.path = v.str("fichier");
        c.adoptedAt = static_cast<Seconds>(v.integer("adoptee"));
        *adopted = std::move(c);
    }
    return true;
}

bool keepCertificate(const std::string& folder, const std::string& certBlock, std::string* why) {
    if (certBlock.empty()) return fail(why, "aucun certificat");
    std::string old;
    if (isFile(at(folder, kFileName)) && readText(at(folder, kFileName), old, nullptr) && old == certBlock) return true;
    return crypto::replaceFile(at(folder, kFileName), certBlock, false, why);
}

std::string exportText(const std::string& folder) {
    std::string text;
    if (!isFile(at(folder, kFileName)) || !readText(at(folder, kFileName), text, nullptr)) return {};
    return text;
}

} // namespace license_admin::connexion
