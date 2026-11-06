// =============================================================================
//  deploy/Pairing.cpp - 1.28.0 : l'appairage (voir Pairing.hpp)
// =============================================================================
#include "Pairing.hpp"

#include "Paths.hpp"
#include "Protocol.hpp"

#include <sodium.h>

#include <algorithm>
#include <filesystem>

namespace deploy::pairing {

namespace {

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool fail(std::string* why, std::string text) {
    if (why) *why = std::move(text);
    return false;
}

bool validCode(const std::string& code) {
    if (code.size() != static_cast<std::size_t>(kCodeDigits)) return false;
    return std::all_of(code.begin(), code.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Un champ hexadecimal de `n` octets, copie dans `out`.
template <std::size_t N>
bool hexField(const core::json::Value& obj, const char* key, std::array<std::uint8_t, N>& out) {
    const auto& v = obj.at(key);
    crypto::Bytes b;
    if (!v.isString() || !crypto::unhex(v.asString(), b, N)) return false;
    std::copy(b.begin(), b.end(), out.begin());
    return true;
}

bool textField(const core::json::Value& obj, const char* key, std::string& out, std::size_t maxLen) {
    const auto& v = obj.at(key);
    if (!v.isString() || v.asString().size() > maxLen) return false;
    out = v.asString();
    return true;
}

// Chaque champ de la conversation precede de sa longueur (4 octets) : aucune ambiguite.
void field(std::string& out, const void* p, std::size_t n) {
    const auto len = static_cast<std::uint32_t>(n);
    out.push_back(static_cast<char>(len >> 24));
    out.push_back(static_cast<char>(len >> 16));
    out.push_back(static_cast<char>(len >> 8));
    out.push_back(static_cast<char>(len));
    out.append(static_cast<const char*>(p), n);
}

std::string transcript(const Conversation& c) {
    std::string t;
    field(t, c.sid.data(), c.sid.size());
    field(t, c.fpStudio.data(), c.fpStudio.size());
    field(t, c.fpAgent.data(), c.fpAgent.size());
    field(t, c.ys.data(), c.ys.size());
    field(t, c.ya.data(), c.ya.size());
    field(t, c.studioName.data(), c.studioName.size());
    field(t, c.studioKeyHex.data(), c.studioKeyHex.size());
    field(t, c.agentName.data(), c.agentName.size());
    field(t, c.agentId.data(), c.agentId.size());
    return t;
}

const char* const kWrongCodeText =
    "l'appairage a \xC3\xA9" "chou\xC3\xA9 : le code ne correspond pas (ou quelqu'un s'interpose entre les deux PC)";

} // namespace

bool generator(const std::string& code, const Sid& sid, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent, Point& g) {
    if (!validCode(code) || !crypto::init()) return false;
    static const char kLabel[] = "XPG-PAIR-v1";
    crypto_hash_sha512_state st;
    crypto_hash_sha512_init(&st);
    crypto_hash_sha512_update(&st, reinterpret_cast<const unsigned char*>(kLabel), sizeof kLabel - 1);
    crypto_hash_sha512_update(&st, reinterpret_cast<const unsigned char*>(code.data()), code.size());
    crypto_hash_sha512_update(&st, sid.data(), sid.size());
    crypto_hash_sha512_update(&st, fpStudio.data(), fpStudio.size());
    crypto_hash_sha512_update(&st, fpAgent.data(), fpAgent.size());
    unsigned char h[crypto_hash_sha512_BYTES];
    crypto_hash_sha512_final(&st, h);
    const int rc = crypto_core_ristretto255_from_hash(g.data(), h);
    sodium_memzero(h, sizeof h);
    return rc == 0;
}

bool newShare(const Point& g, Scalar& y, Point& share) {
    crypto_core_ristretto255_scalar_random(y.data());
    return crypto_scalarmult_ristretto255(share.data(), y.data(), g.data()) == 0;
}

bool sessionKey(const Scalar& y, const Point& other, const Point& ys, const Point& ya, const Sid& sid, SessionKey& isk) {
    // Le point identite (tout a zero) donnerait un K connu de tous : refuse. crypto_scalarmult_ristretto255
    // refuse aussi un point mal forme et un resultat identite.
    if (sodium_is_zero(other.data(), other.size())) return false;
    unsigned char k[32];
    if (crypto_scalarmult_ristretto255(k, y.data(), other.data()) != 0) return false;
    static const char kLabel[] = "XPG-PAIR-ISK";
    crypto_hash_sha512_state st;
    crypto_hash_sha512_init(&st);
    crypto_hash_sha512_update(&st, reinterpret_cast<const unsigned char*>(kLabel), sizeof kLabel - 1);
    crypto_hash_sha512_update(&st, k, sizeof k);
    crypto_hash_sha512_update(&st, ys.data(), ys.size());
    crypto_hash_sha512_update(&st, ya.data(), ya.size());
    crypto_hash_sha512_update(&st, sid.data(), sid.size());
    crypto_hash_sha512_final(&st, isk.data());
    sodium_memzero(k, sizeof k);
    return true;
}

Tag confirmation(const SessionKey& isk, const Conversation& c, bool agentSide) {
    const std::string label = agentSide ? "XPG-PAIR-AGENT" : "XPG-PAIR-STUDIO";
    const std::string t = transcript(c);
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, isk.data(), isk.size());
    crypto_auth_hmacsha256_update(&st, reinterpret_cast<const unsigned char*>(label.data()), label.size());
    crypto_auth_hmacsha256_update(&st, reinterpret_cast<const unsigned char*>(t.data()), t.size());
    Tag tag{};
    crypto_auth_hmacsha256_final(&st, tag.data());
    return tag;
}

// ============================================================ StudioPairing ===
StudioPairing::StudioPairing(std::string code, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent,
                             std::string studioName, std::string publisherKeyHex)
    : code_(std::move(code)) {
    conv_.fpStudio = fpStudio;
    conv_.fpAgent = fpAgent;
    conv_.studioName = std::move(studioName);
    conv_.studioKeyHex = std::move(publisherKeyHex);
}

StudioPairing::~StudioPairing() {
    crypto::wipe(y_);
    crypto::wipe(code_.data(), code_.size());
}

bool StudioPairing::start(core::json::Value& pairStart, std::string* why) {
    if (!validCode(code_)) return fail(why, "le code d'appairage a 6 chiffres");
    crypto::randomBytes(conv_.sid.data(), conv_.sid.size());
    if (!generator(code_, conv_.sid, conv_.fpStudio, conv_.fpAgent, g_) || !newShare(g_, y_, conv_.ys))
        return fail(why, "appairage : calcul impossible");
    pairStart = core::json::Value::object();
    pairStart.set("sid", crypto::hex(conv_.sid));
    pairStart.set("Ys", crypto::hex(conv_.ys));
    pairStart.set("name", conv_.studioName);
    pairStart.set("publisherKey", conv_.studioKeyHex);
    started_ = true;
    return true;
}

bool StudioPairing::answer(const core::json::Value& reply, core::json::Value& pairFinish, std::string* why) {
    if (!started_) return fail(why, "appairage : PAIR_START d'abord");
    Tag got{};
    if (!reply.isObject() || !hexField(reply, "Ya", conv_.ya) || !hexField(reply, "confirm", got)
        || !textField(reply, "name", conv_.agentName, 128) || !textField(reply, "id", conv_.agentId, 32))
        return fail(why, "appairage : r\xC3\xA9ponse de l'agent illisible");
    SessionKey isk{};
    if (!sessionKey(y_, conv_.ya, conv_.ys, conv_.ya, conv_.sid, isk)) return fail(why, "appairage : point de l'agent refus\xC3\xA9");
    const Tag expected = confirmation(isk, conv_, true);
    const bool ok = crypto_verify_32(expected.data(), got.data()) == 0;
    if (ok) {
        pairFinish = core::json::Value::object();
        pairFinish.set("confirm", crypto::hex(confirmation(isk, conv_, false)));
    }
    crypto::wipe(isk);
    crypto::wipe(y_);
    started_ = false;   // un seul essai
    return ok || fail(why, kWrongCodeText);
}

// ============================================================= AgentPairing ===
AgentPairing::AgentPairing(std::string code, const crypto::Digest& fpStudio, const crypto::Digest& fpAgent, std::string agentName,
                           std::string agentId)
    : code_(std::move(code)) {
    conv_.fpStudio = fpStudio;
    conv_.fpAgent = fpAgent;
    conv_.agentName = std::move(agentName);
    conv_.agentId = std::move(agentId);
}

AgentPairing::~AgentPairing() {
    crypto::wipe(isk_);
    crypto::wipe(code_.data(), code_.size());
}

bool AgentPairing::start(const core::json::Value& pairStart, core::json::Value& reply, std::string* why) {
    if (started_) return fail(why, "un seul essai d'appairage par connexion");
    started_ = true;
    crypto::PublicKey key{};
    if (!pairStart.isObject() || !hexField(pairStart, "sid", conv_.sid) || !hexField(pairStart, "Ys", conv_.ys)
        || !textField(pairStart, "name", conv_.studioName, 128) || conv_.studioName.empty()
        || !hexField(pairStart, "publisherKey", key))
        return fail(why, "PAIR_START mal form\xC3\xA9 (sid, Ys, name, publisherKey)");
    conv_.studioKeyHex = crypto::hex(key);
    Point g{};
    Scalar ya{};
    if (!generator(code_, conv_.sid, conv_.fpStudio, conv_.fpAgent, g) || !newShare(g, ya, conv_.ya))
        return fail(why, "appairage : calcul impossible");
    const bool ok = sessionKey(ya, conv_.ys, conv_.ys, conv_.ya, conv_.sid, isk_);
    crypto::wipe(ya);
    if (!ok) return fail(why, "appairage : point du Studio refus\xC3\xA9 (identit\xC3\xA9 ou mal form\xC3\xA9)");
    reply = core::json::Value::object();
    reply.set("Ya", crypto::hex(conv_.ya));
    reply.set("confirm", crypto::hex(confirmation(isk_, conv_, true)));
    reply.set("name", conv_.agentName);
    reply.set("id", conv_.agentId);
    return true;
}

bool AgentPairing::finish(const core::json::Value& pairFinish, std::string* why) {
    if (!started_) return fail(why, "PAIR_START d'abord");
    Tag got{};
    if (!pairFinish.isObject() || !hexField(pairFinish, "confirm", got)) return fail(why, kWrongCodeText);
    const Tag expected = confirmation(isk_, conv_, false);
    crypto::wipe(isk_);
    return crypto_verify_32(expected.data(), got.data()) == 0 || fail(why, kWrongCodeText);
}

// ===================================================================== code ===
std::string codePath(const std::string& agentDir) { return joinPath(agentDir, "appairage.code"); }

bool saveCode(const std::string& agentDir, const CodeState& s, std::string* why) {
    core::json::Value v = core::json::Value::object();
    v.set("code", s.code);
    v.set("expiresAt", s.expiresAt);
    v.set("attempts", s.attempts);
    const std::string text = core::json::write(v, false);
    crypto::Bytes plain(text.begin(), text.end()), sealed;
    const bool ok = crypto::protect(plain, sealed, true, why) && crypto::writePrivateFile(codePath(agentDir), sealed, why);
    crypto::wipe(plain.data(), plain.size());
    return ok;
}

bool createCode(const std::string& agentDir, CodeState& out, std::string* why, std::int64_t validityMs) {
    if (!crypto::init()) return fail(why, "le hasard du syst\xC3\xA8me est indisponible");
    CodeState s;
    s.code = crypto::randomDigits(kCodeDigits);
    s.expiresAt = protocol::nowMs() + validityMs;
    s.attempts = 0;
    if (!saveCode(agentDir, s, why)) return false;
    out = s;
    return true;
}

CodeLoad loadCode(const std::string& agentDir, CodeState& out, std::string* why) {
    std::error_code ec;
    if (!std::filesystem::exists(P(codePath(agentDir)), ec)) return CodeLoad::None;
    crypto::Bytes sealed, plain;
    if (!crypto::readFile(codePath(agentDir), sealed, why, 64 * 1024) || !crypto::unprotect(sealed, plain, why)) return CodeLoad::Error;
    core::json::Value v;
    const bool parsed = core::json::parse(std::string_view(reinterpret_cast<const char*>(plain.data()), plain.size()), v, nullptr);
    crypto::wipe(plain.data(), plain.size());
    if (!parsed || !v.isObject() || !validCode(v.str("code"))) {
        if (why) *why = "le code d'appairage en cours est illisible";
        return CodeLoad::Error;
    }
    out.code = v.str("code");
    out.expiresAt = v.integer("expiresAt");
    out.attempts = static_cast<int>(v.integer("attempts"));
    if (out.attempts >= kMaxAttempts) return CodeLoad::Exhausted;
    if (protocol::nowMs() > out.expiresAt) return CodeLoad::Expired;
    return CodeLoad::Ok;
}

void eraseCode(const std::string& agentDir) {
    std::error_code ec;
    std::filesystem::remove(P(codePath(agentDir)), ec);
}

// =============================================================== StudioStore ===
StudioStore::StudioStore(std::string agentDir) : dir_(std::move(agentDir)) {}

std::string StudioStore::path() const { return joinPath(dir_, "appaires.json"); }

bool StudioStore::load(std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(P(path()), ec)) return true;
    crypto::Bytes raw;
    if (!crypto::readFile(path(), raw, why, 8 * 1024 * 1024)) return false;
    core::json::Value v;
    std::string err;
    if (!core::json::parse(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), v, &err))
        return fail(why, path() + " illisible (" + err + ")");
    for (const auto& s : v.at("studios").items()) {
        PairedStudio p;
        p.fingerprint = s.str("fingerprint");
        p.name = s.str("name");
        p.publisherKey = s.str("publisherKey");
        p.pairedAt = s.integer("pairedAt");
        crypto::Bytes b;
        if (crypto::unhex(p.fingerprint, b, 32)) items_.push_back(std::move(p));
    }
    return true;
}

bool StudioStore::saveLocked(std::string* why) const {
    core::json::Value root = core::json::Value::object();
    root.set("format", "xpg-agent-appaires");
    root.set("version", 1);
    core::json::Value list = core::json::Value::array();
    for (const auto& s : items_) {
        core::json::Value o = core::json::Value::object();
        o.set("fingerprint", s.fingerprint);
        o.set("name", s.name);
        o.set("publisherKey", s.publisherKey);
        o.set("pairedAt", s.pairedAt);
        list.push(std::move(o));
    }
    root.set("studios", std::move(list));
    const std::string text = core::json::write(root, true) + "\n";
    return crypto::writePrivateFile(path(), crypto::Bytes(text.begin(), text.end()), why);
}

bool StudioStore::contains(const std::string& fp) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(items_.begin(), items_.end(), [&](const PairedStudio& s) { return s.fingerprint == fp; });
}

bool StudioStore::find(const std::string& fp, PairedStudio& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& s : items_)
        if (s.fingerprint == fp) {
            out = s;
            return true;
        }
    return false;
}

std::vector<PairedStudio> StudioStore::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

std::size_t StudioStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_.size();
}

bool StudioStore::upsert(const PairedStudio& s, std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(items_.begin(), items_.end(), [&](const PairedStudio& x) { return x.fingerprint == s.fingerprint; });
    if (it != items_.end()) *it = s;
    else items_.push_back(s);
    return saveLocked(why);
}

bool StudioStore::remove(const std::string& fp, std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const PairedStudio& x) { return x.fingerprint == fp; }), items_.end());
    return items_.size() == before || saveLocked(why);
}

} // namespace deploy::pairing

namespace deploy {

namespace {
std::filesystem::path P2(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
} // namespace

// ============================================================== MachineStore ===
MachineStore::MachineStore(std::string dir) : dir_(std::move(dir)) {}

std::string MachineStore::path() const { return joinPath(dir_, "machines-appairees.json"); }

bool MachineStore::load(std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    items_.clear();
    std::error_code ec;
    if (!std::filesystem::exists(P2(path()), ec)) return true;
    crypto::Bytes raw;
    if (!crypto::readFile(path(), raw, why, 8 * 1024 * 1024)) return false;
    core::json::Value v;
    std::string err;
    if (!core::json::parse(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), v, &err)) {
        if (why) *why = path() + " illisible (" + err + ")";
        return false;
    }
    for (const auto& m : v.at("machines").items()) {
        PairedMachine p;
        p.fingerprint = m.str("fingerprint");
        p.name = m.str("name");
        p.id = m.str("id");
        p.address = m.str("address");
        p.port = static_cast<int>(m.integer("port", protocol::kAgentPort));
        p.pairedAt = m.integer("pairedAt");
        crypto::Bytes b;
        if (crypto::unhex(p.fingerprint, b, 32)) items_.push_back(std::move(p));
    }
    return true;
}

bool MachineStore::saveLocked(std::string* why) const {
    core::json::Value root = core::json::Value::object();
    root.set("format", "xpg-machines-appairees");
    root.set("version", 1);
    core::json::Value list = core::json::Value::array();
    for (const auto& m : items_) {
        core::json::Value o = core::json::Value::object();
        o.set("fingerprint", m.fingerprint);
        o.set("name", m.name);
        o.set("id", m.id);
        o.set("address", m.address);
        o.set("port", m.port);
        o.set("pairedAt", m.pairedAt);
        list.push(std::move(o));
    }
    root.set("machines", std::move(list));
    const std::string text = core::json::write(root, true) + "\n";
    return crypto::writePrivateFile(path(), crypto::Bytes(text.begin(), text.end()), why);
}

std::vector<PairedMachine> MachineStore::machines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items_;
}

bool MachineStore::find(const std::string& fp, PairedMachine& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& m : items_)
        if (m.fingerprint == fp) {
            out = m;
            return true;
        }
    return false;
}

bool MachineStore::upsert(const PairedMachine& m, std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(items_.begin(), items_.end(), [&](const PairedMachine& x) { return x.fingerprint == m.fingerprint; });
    if (it != items_.end()) *it = m;
    else items_.push_back(m);
    return saveLocked(why);
}

bool MachineStore::remove(const std::string& fp, std::string* why) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto before = items_.size();
    items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const PairedMachine& x) { return x.fingerprint == fp; }), items_.end());
    return items_.size() == before || saveLocked(why);
}

} // namespace deploy
