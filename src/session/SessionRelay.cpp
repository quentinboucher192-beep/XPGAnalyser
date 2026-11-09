// =============================================================================
//  session/SessionRelay.cpp - 1.44.0 : voir SessionRelay.hpp
// =============================================================================
#include "SessionRelay.hpp"

#include "../deploy/Crypto.hpp"
#include "../deploy/Pairing.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace session {

namespace {

using Clock = std::chrono::steady_clock;
namespace crypto = deploy::crypto;
namespace pairing = deploy::pairing;
using Key = std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_KEYBYTES>;

constexpr const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford
constexpr int kWaitSeconds = 25;          // l'attente longue du relais
constexpr double kHandshakeSeconds = 60;  // l'entree : l'echange du code, BONJOUR, LIEN_OK
constexpr double kOfflineSeconds = 120;   // le relais ne repond plus depuis 2 min : fini
constexpr double kSendGap = 0.15;         // entre deux envois (les limites du relais)
constexpr double kReadGap = 0.08;         // apres une lecture qui a rendu des messages
constexpr double kFlushSeconds = 2.5;     // stop() : ce qui attend part, la fermeture est dite au relais
constexpr std::size_t kQueued = 256;      // ce qu'une trame en file compte en plus de ses donnees (pending)

double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

core::json::Value op(std::string_view name) {
    core::json::Value v = core::json::Value::object();
    v.set("op", std::string(name));
    return v;
}

void put32(std::string& s, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
std::uint32_t get32(const char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<std::uint8_t>(p[i]);
    return v;
}
void put64(std::string& s, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
std::uint64_t get64(const char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint8_t>(p[i]);
    return v;
}

template <std::size_t N>
bool hexField(const core::json::Value& v, const char* key, std::array<std::uint8_t, N>& out) {
    crypto::Bytes b;
    if (!v.at(key).isString() || !crypto::unhex(v.at(key).asString(), b, N)) return false;
    std::copy(b.begin(), b.end(), out.begin());
    return true;
}

// ---- l'echange du code (CPace, les briques de deploy::pairing) ----
crypto::Digest canalOf(const std::string& locator, int conn) { return crypto::sha256("XPG-RELAIS-CANAL|" + locator + "|" + std::to_string(conn)); }

bool generator(const std::string& code, const pairing::Sid& sid, const crypto::Digest& canal, pairing::Point& g) {
    if (code.size() != static_cast<std::size_t>(kRelayCodeChars) || !crypto::init()) return false;
    static const char kLabel[] = "XPG-RELAIS-v1";
    const unsigned char len = static_cast<unsigned char>(code.size());
    crypto_hash_sha512_state st;
    crypto_hash_sha512_init(&st);
    crypto_hash_sha512_update(&st, reinterpret_cast<const unsigned char*>(kLabel), sizeof kLabel - 1);
    crypto_hash_sha512_update(&st, &len, 1);
    crypto_hash_sha512_update(&st, reinterpret_cast<const unsigned char*>(code.data()), code.size());
    crypto_hash_sha512_update(&st, sid.data(), sid.size());
    crypto_hash_sha512_update(&st, canal.data(), canal.size());
    unsigned char h[crypto_hash_sha512_BYTES];
    crypto_hash_sha512_final(&st, h);
    const int rc = crypto_core_ristretto255_from_hash(g.data(), h);
    sodium_memzero(h, sizeof h);
    return rc == 0;
}

// Ce que les deux confirmations couvrent : le localisateur, la connexion, les deux parts.
pairing::Conversation conversation(const std::string& locator, int conn, const pairing::Sid& sid, const pairing::Point& ys,
                                   const pairing::Point& ya) {
    pairing::Conversation c;
    c.sid = sid;
    c.fpStudio = canalOf(locator, conn);
    c.fpAgent = crypto::sha256("XPG-RELAIS-HOTE|" + locator);
    c.ys = ys;
    c.ya = ya;
    c.studioName = "equipier";
    c.studioKeyHex = std::to_string(conn);
    c.agentName = "hote";
    c.agentId = locator;
    return c;
}

// Une cle par sens, tiree de la cle de l'echange.
void trafficKeys(const pairing::SessionKey& isk, bool host, Key& tx, Key& rx) {
    static const char kToHost[] = "XPG-RELAIS-EQUIPIER>HOTE";
    static const char kToGuest[] = "XPG-RELAIS-HOTE>EQUIPIER";
    Key toHost{}, toGuest{};
    crypto_generichash(toHost.data(), toHost.size(), reinterpret_cast<const unsigned char*>(kToHost), sizeof kToHost - 1, isk.data(), isk.size());
    crypto_generichash(toGuest.data(), toGuest.size(), reinterpret_cast<const unsigned char*>(kToGuest), sizeof kToGuest - 1, isk.data(),
                       isk.size());
    tx = host ? toGuest : toHost;
    rx = host ? toHost : toGuest;
    crypto::wipe(toHost);
    crypto::wipe(toGuest);
}

// ---- un pair (l'hote : un equipier ; l'equipier : l'hote) ----
struct Peer {
    enum class Stage { Fresh, Answered, Keyed, Open };
    int conn{0};
    bool host{false};               // CE cote est l'hote
    std::string locator;
    std::string key;                // (l'hote) la cle du PC de l'equipier, dite par le relais
    Stage stage{Stage::Fresh};
    pairing::Sid sid{};
    pairing::Scalar y{};
    pairing::Point ys{}, ya{};
    pairing::SessionKey isk{};
    Key tx{}, rx{};
    std::uint64_t txCount{0}, rxCount{0};
    std::string partial;            // la trame qui arrive en morceaux
    std::deque<std::pair<core::json::Value, std::string>> frames;   // a sceller (le lien ouvert)
    std::size_t frameBytes{0};
    std::deque<std::string> records;   // prets a partir, dans l'ordre
    std::size_t recordBytes{0};
    bool closeWhenFlushed{false};
    std::string closeReason;        // dit au relais (la connexion renvoyee)
    bool announced{false};          // (l'hote) Knock pose
    double since{0};
    double refusedUntil{0};         // (l'hote) sa boite etait pleine

    Peer() = default;
    Peer(Peer&&) = default;
    Peer& operator=(Peer&&) = default;
    ~Peer() {
        crypto::wipe(y);
        crypto::wipe(isk);
        crypto::wipe(tx);
        crypto::wipe(rx);
    }

    void pushRecord(std::string r) {
        recordBytes += r.size();
        records.push_back(std::move(r));
    }
    void pushClear(const core::json::Value& v) { pushRecord("C" + core::json::write(v, false)); }

    std::string associated(bool fromHost) const {
        return "XPG-RELAIS-v1|" + locator + "|" + std::to_string(conn) + "|" + (fromHost ? "H" : "E");
    }
    static std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> nonce(bool fromHost, std::uint64_t n) {
        std::array<std::uint8_t, crypto_aead_xchacha20poly1305_ietf_NPUBBYTES> out{};
        out[0] = fromHost ? 'H' : 'E';
        for (int i = 0; i < 8; ++i) out[out.size() - 1 - static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((n >> (8 * i)) & 0xFF);
        return out;
    }

    // Sceller un morceau : 'S' + le compteur + le texte scelle (1 octet << la suite vient >>, le morceau).
    void sealPiece(std::string_view piece, bool more) {
        std::string plain;
        plain.reserve(piece.size() + 1);
        plain.push_back(more ? 1 : 0);
        plain.append(piece);
        const std::uint64_t n = txCount++;
        const auto nc = nonce(host, n);
        const std::string ad = associated(host);
        std::string out = "S";
        put64(out, n);
        const std::size_t at = out.size();
        out.resize(at + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        unsigned long long len = 0;
        crypto_aead_xchacha20poly1305_ietf_encrypt(reinterpret_cast<unsigned char*>(out.data()) + at, &len,
                                                   reinterpret_cast<const unsigned char*>(plain.data()), plain.size(),
                                                   reinterpret_cast<const unsigned char*>(ad.data()), ad.size(), nullptr, nc.data(), tx.data());
        out.resize(at + static_cast<std::size_t>(len));
        sodium_memzero(plain.data(), plain.size());
        pushRecord(std::move(out));
    }

    void sealFrame(const core::json::Value& h, std::string_view data) {
        const std::string bytes = relay::encodeFrame(h, data);
        std::size_t at = 0;
        do {
            const std::size_t n = std::min(relay::kPiece, bytes.size() - at);
            sealPiece(std::string_view(bytes).substr(at, n), at + n < bytes.size());
            at += n;
        } while (at < bytes.size());
    }

    // Les trames en file (le lien ouvert), juste ce qu'il faut pour un envoi.
    void sealQueued() {
        if (stage != Stage::Open) return;
        while (!frames.empty() && recordBytes < relay::kBatch) {
            auto f = std::move(frames.front());
            frames.pop_front();
            frameBytes -= std::min(frameBytes, f.second.size() + kQueued);
            sealFrame(f.first, f.second);
        }
    }

    enum class Unsealed { Ok, Duplicate, Bad };
    // Ouvrir un enregistrement scelle de l'autre cote : un compteur deja vu est ignore (un envoi refait, un rejeu), un
    // compteur plus loin (un message perdu, le desordre) ou un texte modifie est refuse.
    Unsealed unseal(std::string_view rec, std::string& piece, bool& more) {
        if (rec.size() < 1 + 8 + crypto_aead_xchacha20poly1305_ietf_ABYTES + 1) return Unsealed::Bad;
        const std::uint64_t n = get64(rec.data() + 1);
        if (n < rxCount) return Unsealed::Duplicate;
        if (n > rxCount) return Unsealed::Bad;
        const auto nc = nonce(!host, n);
        const std::string ad = associated(!host);
        const std::string_view sealed = rec.substr(9);
        std::string plain(sealed.size() - crypto_aead_xchacha20poly1305_ietf_ABYTES, '\0');
        unsigned long long len = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(reinterpret_cast<unsigned char*>(plain.data()), &len, nullptr,
                                                       reinterpret_cast<const unsigned char*>(sealed.data()), sealed.size(),
                                                       reinterpret_cast<const unsigned char*>(ad.data()), ad.size(), nc.data(), rx.data())
            != 0)
            return Unsealed::Bad;
        plain.resize(static_cast<std::size_t>(len));
        if (plain.empty()) return Unsealed::Bad;
        ++rxCount;
        more = (plain[0] & 1) != 0;
        piece.assign(plain, 1, std::string::npos);
        sodium_memzero(plain.data(), plain.size());
        return Unsealed::Ok;
    }

    // Un morceau recu ; vrai et `done` : une trame entiere (`h`, `data`). Faux : `why`.
    bool takePiece(const std::string& piece, bool more, bool& done, core::json::Value& h, std::string& data, std::string& why) {
        done = false;
        partial += piece;
        if (partial.size() > relay::kMaxFrame) {
            why = "trame trop grosse";
            return false;
        }
        if (more) return true;
        const bool ok = relay::decodeFrame(partial, h, data);
        partial.clear();
        if (!ok) {
            why = "trame illisible";
            return false;
        }
        done = true;
        return true;
    }
};

// ---- les appels au relais ----
struct Answer {
    int status{0};
    core::json::Value json;
    std::string error;
};

Answer ask(RelayTransport& t, const std::string& method, const std::string& path, const std::string& body, int wait = 0) {
    RelayCall c;
    c.method = method;
    c.path = path;
    c.body = body;
    c.waitSeconds = wait;
    const RelayReply r = t.call(c);
    Answer a;
    a.status = r.status;
    a.error = r.error;
    if (r.status) (void)core::json::parse(r.body, a.json);
    return a;
}

// Ce que dit le relais (sa phrase, sans le point final ; une majuscule ASCII en tete baissee).
std::string said(const Answer& a) {
    std::string m;
    if (a.status == 0) m = a.error.empty() ? std::string("le service XPG ne r\xC3\xA9pond pas") : a.error;
    else m = a.json.str("message");
    if (m.empty()) m = "le service XPG r\xC3\xA9pond " + std::to_string(a.status);
    while (!m.empty() && (m.back() == '.' || m.back() == ' ')) m.pop_back();
    if (!m.empty() && m[0] >= 'A' && m[0] <= 'Z' && !(m.size() > 1 && m[1] >= 'A' && m[1] <= 'Z')) m[0] = static_cast<char>(m[0] - 'A' + 'a');
    return m;
}

// 1.45.0 : un service d'avant la 1.44 (pas de relais) : 404 << route >> sur le service des licences lui-meme (un
// relais 1.44 dit << relais des sessions >> dans sa phrase : une adresse fausse du relais, pas un service trop vieux).
bool serviceTooOld(const Answer& a) {
    if (a.status != 404 || a.json.str("erreur") != "route") return false;
    return a.json.str("message").find("relais") == std::string::npos;
}

int retryAfter(const Answer& a, int fallback) {
    const auto v = a.json.integer("reessayer_dans", fallback);
    return static_cast<int>(std::clamp<std::int64_t>(v, 1, 30));
}

core::json::Value refusal(const std::string& reason, bool wrongCode, int left) {
    core::json::Value r = op("REFUS");
    r.set("raison", reason);
    if (wrongCode) {
        r.set("code_faux", true);
        r.set("essais", left);
    }
    return r;
}

std::string triesText(int left) {
    return left > 0 ? "code faux (encore " + std::to_string(left) + " essai" + (left > 1 ? "s" : "") + ")"
                    : std::string("code faux : plus d'essai depuis ce PC (demande \xC3\xA0 l'h\xC3\xB4te un nouveau code)");
}

// Ce qui est commun aux deux liens : le verrou, le reveil, les evenements, l'arret.
struct Core {
    std::mutex m;
    std::condition_variable cv;
    std::vector<LinkEvent> events;
    std::atomic<bool> stopping{false}, closing{false}, finished{false}, dead{false};
    std::thread reader, writer;

    void post(LinkEvent e) { events.push_back(std::move(e)); }   // sous le verrou
    // Dormir (`s` secondes), reveille par l'arret.
    void sleepFor(double s) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::duration<double>(std::max(0.0, s)), [&] { return stopping.load(); });
    }
    std::vector<LinkEvent> take() {
        std::vector<LinkEvent> out;
        std::lock_guard<std::mutex> lock(m);
        out.swap(events);
        return out;
    }
    // Ce qui attend part (kFlushSeconds au plus), puis les fils s'arretent.
    void halt(RelayTransport* t) {
        if (!reader.joinable() && !writer.joinable()) return;
        closing = true;
        cv.notify_all();
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, std::chrono::duration<double>(kFlushSeconds), [&] { return finished.load(); });
        }
        stopping = true;
        cv.notify_all();
        if (t) t->cancel();
        if (reader.joinable()) reader.join();
        if (writer.joinable()) writer.join();
    }
};

} // namespace

// ============================================================================================== le code ===
std::string newRelayCode() {
    (void)crypto::init();
    std::uint8_t b[kRelayCodeChars];
    crypto::randomBytes(b, sizeof b);
    std::string out;
    for (const auto x : b) out.push_back(kAlphabet[x & 31]);   // 256 = 8 x 32 : uniforme
    crypto::wipe(b, sizeof b);
    return out;
}

bool relayCodeTyped(std::string_view text, std::string& code) {
    std::string out;
    for (const char ch : text) {
        const auto u = static_cast<unsigned char>(ch);
        if (u == ' ' || u == '-' || u == '_' || u == '.' || u == '\t' || u == '\r' || u == '\n') continue;
        char c = (u >= 'a' && u <= 'z') ? static_cast<char>(u - 'a' + 'A') : static_cast<char>(u);
        if (c == 'O') c = '0';
        else if (c == 'I' || c == 'L') c = '1';
        if (c == '\0' || std::strchr(kAlphabet, c) == nullptr) return false;
        out.push_back(c);
        if (out.size() > static_cast<std::size_t>(kRelayCodeChars)) return false;
    }
    if (out.size() != static_cast<std::size_t>(kRelayCodeChars)) return false;
    code = std::move(out);
    return true;
}

std::string relayCodeShown(std::string_view code) {
    std::string n;
    if (!relayCodeTyped(code, n)) return std::string(code);
    return n.substr(0, 4) + "-" + n.substr(4, 4) + "-" + n.substr(8);
}

std::string relayLocator(std::string_view code) {
    // Argon2id (libsodium) : lent et gourmand en memoire - le service ne retrouve pas le code en essayant toutes les
    // combinaisons a partir du localisateur. Le sel est fixe (le localisateur doit etre le meme sur chaque PC).
    std::string n;
    if (!relayCodeTyped(code, n)) n = std::string(code);
    if (!crypto::init()) return {};
    const auto salt = crypto::sha256(std::string("XPG-RELAIS-v1"));
    static_assert(crypto_pwhash_SALTBYTES <= 32, "le sel : les premiers octets de SHA-256");
    unsigned char out[16];
    if (crypto_pwhash(out, sizeof out, n.data(), n.size(), salt.data(), kRelayLocatorOps, kRelayLocatorMem, crypto_pwhash_ALG_ARGON2ID13) != 0) {
        crypto::wipe(n.data(), n.size());
        return {};   // la memoire manque
    }
    crypto::wipe(n.data(), n.size());
    return crypto::hex(out, sizeof out);
}

// ========================================================================================= les briques ===
namespace relay {

std::string packRecords(const std::vector<std::string>& records) {
    std::string out;
    std::size_t n = 0;
    for (const auto& r : records) n += r.size() + 4;
    out.reserve(n);
    for (const auto& r : records) {
        put32(out, static_cast<std::uint32_t>(r.size()));
        out += r;
    }
    return out;
}

bool unpackRecords(std::string_view data, std::vector<std::string>& records) {
    records.clear();
    std::size_t at = 0;
    while (at < data.size()) {
        if (data.size() - at < 4) return false;
        const std::uint32_t n = get32(data.data() + at);
        at += 4;
        if (n == 0 || n > data.size() - at) return false;
        records.emplace_back(data.substr(at, n));
        at += n;
    }
    return true;
}

std::string encodeFrame(const core::json::Value& header, std::string_view data) {
    const std::string h = core::json::write(header, false);
    std::string out;
    out.reserve(4 + h.size() + data.size());
    put32(out, static_cast<std::uint32_t>(h.size()));
    out += h;
    out.append(data);
    return out;
}

bool decodeFrame(std::string_view bytes, core::json::Value& header, std::string& data) {
    if (bytes.size() < 4) return false;
    const std::uint32_t n = get32(bytes.data());
    if (n > bytes.size() - 4) return false;
    core::json::Value h;
    if (!core::json::parse(bytes.substr(4, n), h) || !h.isObject()) return false;
    header = std::move(h);
    data.assign(bytes.substr(4 + n));
    return true;
}

} // namespace relay

// ============================================================================================== l'hote ===
struct RelayHostLink::Impl : Core {
    RelayHostConfig cfg;
    std::string locator;
    std::atomic<bool> isOpen{false};
    std::map<int, Peer> peers;
    std::set<int> gone;                               // les connexions fermees (un message en retard est ignore)
    std::map<std::string, int> attempts;              // par cle de PC : les codes faux
    std::vector<std::pair<int, std::string>> kicks;   // les connexions a fermer sur le relais (et pourquoi)
    bool closedSaid{false};
    std::size_t turn{0};   // l'envoi suivant commence par ce pair

    std::string base() const { return "/v1/relais/" + locator; }

    void closeSession(const std::string& why) {   // sous le verrou
        if (closedSaid) return;
        closedSaid = true;
        dead = true;
        LinkEvent e;
        e.kind = LinkEvent::Kind::Closed;
        e.conn = 0;
        e.why = why;
        post(std::move(e));
        cv.notify_all();
    }

    // Une connexion fermee de ce cote : dite au relais (renvoyee), a l'application si elle l'a vue frapper.
    void drop(int conn, const std::string& why, const std::string& relayReason) {   // sous le verrou
        const auto it = peers.find(conn);
        if (it == peers.end()) return;
        LinkEvent e;
        e.kind = LinkEvent::Kind::Closed;
        e.conn = conn;
        e.why = why;
        e.fingerprint = it->second.key;
        e.address = "par Internet";
        post(std::move(e));
        kicks.emplace_back(conn, relayReason);
        gone.insert(conn);
        peers.erase(it);
        cv.notify_all();
    }

    // Un enregistrement d'un equipier. Faux : la connexion est a fermer (`why`).
    bool record(Peer& p, const std::string& rec, std::string& why) {
        if (p.closeWhenFlushed) return true;   // un refus part : le reste est ignore
        if (rec[0] == 'C') {
            core::json::Value v;
            if (!core::json::parse(std::string_view(rec).substr(1), v) || !v.isObject()) {
                why = "message illisible";
                return false;
            }
            const std::string o = v.str("op");
            if (p.stage == Peer::Stage::Fresh && o == "CODE_DEBUT") {
                int& n = attempts[p.key];
                if (n >= cfg.attempts) {
                    p.pushClear(refusal("trop de codes faux depuis ce PC : demande \xC3\xA0 l'h\xC3\xB4te un nouveau code", true, 0));
                    p.closeWhenFlushed = true;
                    p.closeReason = "trop de codes faux";
                    return true;
                }
                pairing::Point g{};
                if (!hexField(v, "sid", p.sid) || !hexField(v, "Y", p.ys) || !generator(cfg.code, p.sid, canalOf(locator, p.conn), g)
                    || !pairing::newShare(g, p.y, p.ya) || !pairing::sessionKey(p.y, p.ys, p.ys, p.ya, p.sid, p.isk)) {
                    p.pushClear(refusal("l'\xC3\xA9" "change du code est refus\xC3\xA9", false, 0));
                    p.closeWhenFlushed = true;
                    p.closeReason = "\xC3\xA9" "change refus\xC3\xA9";
                    return true;
                }
                // L'essai compte des que l'hote repond (comme sur le reseau du bureau) ; un essai reussi est rendu.
                ++n;
                core::json::Value r = op("CODE_REPONSE");
                r.set("Y", crypto::hex(p.ya));
                r.set("confirmation", crypto::hex(pairing::confirmation(p.isk, conversation(locator, p.conn, p.sid, p.ys, p.ya), true)));
                r.set("essais", cfg.attempts - n);
                p.pushClear(r);
                crypto::wipe(p.y);
                p.stage = Peer::Stage::Answered;
                return true;
            }
            if (p.stage == Peer::Stage::Answered && o == "CODE_FIN") {
                pairing::Tag got{};
                const pairing::Tag expected = pairing::confirmation(p.isk, conversation(locator, p.conn, p.sid, p.ys, p.ya), false);
                if (hexField(v, "confirmation", got) && crypto_verify_32(expected.data(), got.data()) == 0) {
                    --attempts[p.key];
                    trafficKeys(p.isk, true, p.tx, p.rx);
                    crypto::wipe(p.isk);
                    p.stage = Peer::Stage::Keyed;
                    return true;
                }
                const int left = std::max(0, cfg.attempts - attempts[p.key]);
                p.pushClear(refusal(triesText(left), true, left));
                p.closeWhenFlushed = true;
                p.closeReason = "code faux";
                return true;
            }
            if (o == "AUREVOIR") {
                why = "parti";
                return false;
            }
            return true;   // autre chose : ignore
        }
        if (rec[0] != 'S' || (p.stage != Peer::Stage::Keyed && p.stage != Peer::Stage::Open)) {
            why = "message inattendu";
            return false;
        }
        std::string piece;
        bool more = false;
        switch (p.unseal(rec, piece, more)) {
            case Peer::Unsealed::Duplicate: return true;
            case Peer::Unsealed::Bad:
                why = "message refus\xC3\xA9 (modifi\xC3\xA9, perdu ou dans le d\xC3\xA9sordre)";
                return false;
            default: break;
        }
        core::json::Value h;
        std::string data;
        bool done = false;
        if (!p.takePiece(piece, more, done, h, data, why)) return false;
        if (!done) return true;
        if (p.stage == Peer::Stage::Keyed) {
            if (h.str("op") != "BONJOUR") {
                why = "BONJOUR attendu";
                return false;
            }
            p.stage = Peer::Stage::Open;
            core::json::Value ok = op("LIEN_OK");
            ok.set("code", true);
            p.sealFrame(ok, {});
            p.announced = true;
            LinkEvent k;
            k.kind = LinkEvent::Kind::Knock;
            k.conn = p.conn;
            k.header = std::move(h);
            k.fingerprint = p.key;
            k.address = "par Internet";
            k.codeOk = true;
            post(std::move(k));
            return true;
        }
        if (h.str("op") == "PING") return true;
        LinkEvent e;
        e.kind = LinkEvent::Kind::Frame;
        e.conn = p.conn;
        e.header = std::move(h);
        e.data = std::move(data);
        post(std::move(e));
        return true;
    }

    void message(const core::json::Value& msg) {   // sous le verrou
        const auto& de = msg.at("de");
        if (!de.isNumber()) return;
        const auto c = de.asInt();
        if (c <= 0 || c > 0x7FFFFFFF) return;
        const int conn = static_cast<int>(c);
        auto it = peers.find(conn);
        if (msg.flag("fin")) {
            if (it != peers.end()) {
                LinkEvent e;
                e.kind = LinkEvent::Kind::Closed;
                e.conn = conn;
                e.why = msg.str("raison") == "parti" ? std::string("l'autre PC a ferm\xC3\xA9 la connexion") : msg.str("raison");
                e.fingerprint = it->second.key;
                e.address = "par Internet";
                post(std::move(e));
                peers.erase(it);
            }
            gone.insert(conn);
            return;
        }
        if (gone.count(conn)) return;
        crypto::Bytes raw;
        if (!crypto::unbase64(msg.str("donnees"), raw)) return;
        std::vector<std::string> recs;
        if (it == peers.end()) {
            if (peers.size() >= 32) return;   // une frappe en rafale : on ne suit pas
            Peer p;
            p.conn = conn;
            p.host = true;
            p.locator = locator;
            p.key = msg.str("cle");
            p.since = now();
            it = peers.emplace(conn, std::move(p)).first;
        }
        if (!relay::unpackRecords(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), recs)) {
            drop(conn, "donn\xC3\xA9" "es illisibles", "donn\xC3\xA9" "es illisibles");
            return;
        }
        for (const auto& r : recs) {
            std::string why;
            if (!record(it->second, r, why)) {
                drop(conn, why, why);
                return;
            }
        }
    }

    // L'entree trop longue : la connexion est fermee.
    void expire() {   // sous le verrou
        const double t = now();
        std::vector<int> late;
        for (const auto& [c, p] : peers)
            if (p.stage != Peer::Stage::Open && !p.closeWhenFlushed && t - p.since > kHandshakeSeconds) late.push_back(c);
        for (const int c : late) drop(c, "l'entr\xC3\xA9" "e n'a pas abouti en 60 s", "l'entr\xC3\xA9" "e n'a pas abouti");
    }

    void readLoop() {
        auto& t = *cfg.transport;
        // Le localisateur (Argon2id : ~0,2 s, ici et pas dans le fil de l'interface).
        {
            std::string loc = relayLocator(cfg.code);
            std::lock_guard<std::mutex> lock(m);
            if (loc.empty()) {
                closeSession("la cl\xC3\xA9 de recherche de la session ne se calcule pas (m\xC3\xA9moire)");
                return;
            }
            locator = std::move(loc);
        }
        if (stopping) return;
        core::json::Value b = core::json::Value::object();
        b.set("locator", locator);
        b.set("nom", cfg.sessionName);
        b.set("hote", cfg.hostName);
        b.set("projet", cfg.project);
        const std::string body = core::json::write(b, false);
        for (int tries = 0;; ++tries) {
            const Answer a = ask(t, "POST", "/v1/relais/ouvrir", body);
            if (stopping) return;
            if (a.status == 200 && a.json.flag("ok")) break;
            if ((a.status == 0 || a.status >= 500) && tries < 2) {
                sleepFor(1.0 + tries);
                if (stopping) return;
                continue;
            }
            std::lock_guard<std::mutex> lock(m);
            // 1.45.0 : un service des licences en 1.43 (pas de relais) : la phrase qui dit quoi faire.
            closeSession(serviceTooOld(a) ? relayTooOldText() : "la session ne s'ouvre pas sur le service XPG : " + said(a));
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m);
            isOpen = true;
            LinkEvent e;
            e.kind = LinkEvent::Kind::Opened;
            e.conn = 0;
            post(std::move(e));
        }
        cv.notify_all();
        std::uint64_t since = 0;
        double failSince = 0, backoff = 1;
        while (!stopping && !dead) {
            const Answer a = ask(t, "GET", base() + "/recevoir?depuis=" + std::to_string(since) + "&attente=" + std::to_string(kWaitSeconds), {},
                                 kWaitSeconds);
            if (stopping) break;
            if (a.status == 200) {
                failSince = 0;
                backoff = 1;
                const auto& msgs = a.json.at("messages").items();
                {
                    std::lock_guard<std::mutex> lock(m);
                    for (const auto& msg : msgs) message(msg);
                    expire();
                }
                const auto next = a.json.integer("suivant", static_cast<std::int64_t>(since));
                if (next >= 0) since = static_cast<std::uint64_t>(next);
                cv.notify_all();
                if (!msgs.empty()) sleepFor(kReadGap);
                continue;
            }
            if (a.status == 404 || a.status == 410 || a.status == 403) {
                std::lock_guard<std::mutex> lock(m);
                closeSession("la session n'existe plus sur le service XPG (" + said(a) + ")");
                break;
            }
            if (a.status == 429 || a.status == 503) {
                sleepFor(retryAfter(a, 2));
                continue;
            }
            if (failSince == 0) failSince = now();
            if (now() - failSince > kOfflineSeconds) {
                std::lock_guard<std::mutex> lock(m);
                closeSession("plus de connexion au service XPG depuis 2 min (" + said(a) + ")");
                break;
            }
            sleepFor(backoff);
            backoff = std::min(backoff * 2, 10.0);
        }
    }

    bool work() const {   // sous le verrou
        if (!kicks.empty()) return true;
        const double t = now();
        for (const auto& [c, p] : peers) {
            (void)c;
            if (p.closeWhenFlushed && p.records.empty()) return true;
            if (t < p.refusedUntil) continue;
            if (!p.records.empty() || (p.stage == Peer::Stage::Open && !p.frames.empty())) return true;
        }
        return false;
    }

    void writeLoop() {
        auto& t = *cfg.transport;
        double lastSend = 0, retryAt = 0, backoff = 1;
        for (;;) {
            struct Taken {
                int conn;
                std::size_t count;
            };
            std::vector<Taken> taken;
            core::json::Value envois = core::json::Value::array();
            std::vector<std::pair<int, std::string>> kicksNow;
            bool exiting = false;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait_for(lock, std::chrono::milliseconds(200),
                            [&] { return stopping.load() || closing.load() || dead.load() || (isOpen && now() >= retryAt && work()); });
                if (stopping) break;
                if (dead) {
                    finished = true;
                    cv.notify_all();
                    return;
                }
                if (!isOpen) {
                    if (closing) {
                        finished = true;
                        cv.notify_all();
                        return;
                    }
                    continue;
                }
                // Ceux a renvoyer, leur refus parti.
                std::vector<int> done;
                for (const auto& [c, p] : peers)
                    if (p.closeWhenFlushed && p.records.empty()) done.push_back(c);
                for (const int c : done) {
                    const std::string reason = peers[c].closeReason;
                    drop(c, reason, reason);
                }
                kicksNow.swap(kicks);
                if (closing) kicksNow.clear();   // la fermeture de la session les ferme toutes
                if (now() >= retryAt) {
                    // Chacun son tour en tete d'un envoi : le projet qui part vers l'un ne retient pas les autres.
                    std::vector<int> order;
                    for (const auto& [c, p] : peers) {
                        (void)p;
                        order.push_back(c);
                    }
                    if (!order.empty()) std::rotate(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(turn++ % order.size()), order.end());
                    std::size_t total = 0;
                    for (const int c : order) {
                        auto& p = peers[c];
                        p.sealQueued();
                        if (p.records.empty() || now() < p.refusedUntil) continue;
                        std::vector<std::string> part;
                        std::size_t bytes = 0;
                        for (const auto& r : p.records) {
                            if (!part.empty() && total + bytes + r.size() + 4 > relay::kBatch) break;
                            if (part.empty() && total > 0 && total + r.size() + 4 > relay::kBatch) break;
                            bytes += r.size() + 4;
                            part.push_back(r);
                        }
                        if (part.empty()) break;
                        const std::string packed = relay::packRecords(part);
                        core::json::Value e = core::json::Value::object();
                        e.set("a", static_cast<std::int64_t>(c));
                        e.set("donnees", crypto::base64(packed.data(), packed.size()));
                        envois.push(std::move(e));
                        taken.push_back({c, part.size()});
                        total += bytes;
                        if (total >= relay::kBatch) break;
                    }
                }
                exiting = closing && taken.empty() && kicksNow.empty() && !work();
            }
            for (const auto& [c, why] : kicksNow) {
                core::json::Value b = core::json::Value::object();
                b.set("conn", static_cast<std::int64_t>(c));
                if (!why.empty()) b.set("raison", why);
                (void)ask(t, "POST", base() + "/fermer", core::json::write(b, false));
                if (stopping) break;
            }
            if (exiting) {
                // La session est fermee pour tous sur le relais (les equipiers recoivent la fin).
                (void)ask(t, "POST", base() + "/fermer", "{}");
                finished = true;
                cv.notify_all();
                return;
            }
            if (taken.empty()) continue;
            if (const double gap = now() - lastSend; gap < kSendGap) sleepFor(kSendGap - gap);
            if (stopping) break;
            core::json::Value body = core::json::Value::object();
            body.set("envois", std::move(envois));
            const Answer a = ask(t, "POST", base() + "/envoyer", core::json::write(body, false));
            lastSend = now();
            if (stopping) break;
            std::lock_guard<std::mutex> lock(m);
            if (a.status == 200) {
                backoff = 1;
                std::set<std::int64_t> refused, unknown;
                for (const auto& x : a.json.at("refuses").items()) refused.insert(x.asInt());
                for (const auto& x : a.json.at("inconnues").items()) unknown.insert(x.asInt());
                for (const auto& tk : taken) {
                    auto it = peers.find(tk.conn);
                    if (it == peers.end()) continue;
                    if (unknown.count(tk.conn)) {
                        drop(tk.conn, "l'autre PC a ferm\xC3\xA9 la connexion", {});
                        continue;
                    }
                    if (refused.count(tk.conn)) {
                        it->second.refusedUntil = now() + retryAfter(a, 1);
                        continue;
                    }
                    auto& p = it->second;
                    for (std::size_t i = 0; i < tk.count && !p.records.empty(); ++i) {
                        p.recordBytes -= std::min(p.recordBytes, p.records.front().size());
                        p.records.pop_front();
                    }
                }
                cv.notify_all();
            } else if (a.status == 404 || a.status == 410) {
                closeSession("la session n'existe plus sur le service XPG (" + said(a) + ")");
            } else if (a.status == 429 || a.status == 503) {
                retryAt = now() + retryAfter(a, 1);
            } else if (a.status == 0 || a.status >= 500) {
                retryAt = now() + backoff;   // refait tel quel : un envoi passe deux fois est ignore de l'autre cote
                backoff = std::min(backoff * 2, 10.0);
            } else {
                // Un refus (400, 403, 413) : ces envois ne passeront pas ; leurs connexions sont fermees.
                for (const auto& tk : taken) drop(tk.conn, "envoi refus\xC3\xA9 par le service XPG : " + said(a), "envoi refus\xC3\xA9");
            }
        }
    }
};

RelayHostLink::RelayHostLink() : d_(std::make_shared<Impl>()) {}
RelayHostLink::~RelayHostLink() { stop(); }

bool RelayHostLink::start(const RelayHostConfig& cfg, std::string* why) {
    stop();
    const auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!cfg.transport) return fail("pas de service XPG pour les sessions par Internet");
    std::string code;
    if (!relayCodeTyped(cfg.code, code)) return fail("le code de la session est mal form\xC3\xA9");
    if (!crypto::init()) return fail("la cryptographie ne d\xC3\xA9marre pas");
    d_ = std::make_shared<Impl>();
    d_->cfg = cfg;
    d_->cfg.code = code;   // le localisateur : calcule dans le fil du lien (Argon2id)
    auto* d = d_.get();
    d_->reader = std::thread([d] { d->readLoop(); });
    d_->writer = std::thread([d] { d->writeLoop(); });
    return true;
}

void RelayHostLink::stop() {
    if (!d_) return;
    d_->halt(d_->cfg.transport.get());
}

bool RelayHostLink::running() const noexcept { return d_ && d_->reader.joinable() && !d_->dead && !d_->stopping; }
bool RelayHostLink::opened() const noexcept { return d_ && d_->isOpen; }
std::string RelayHostLink::locator() const {
    if (!d_) return {};
    std::lock_guard<std::mutex> lock(d_->m);
    return d_->locator;
}

std::vector<LinkEvent> RelayHostLink::poll() { return d_->take(); }

void RelayHostLink::send(int conn, const core::json::Value& header, std::string data) {
    {
        std::lock_guard<std::mutex> lock(d_->m);
        const auto it = d_->peers.find(conn);
        if (it == d_->peers.end() || it->second.closeWhenFlushed) return;
        it->second.frameBytes += data.size() + kQueued;
        it->second.frames.emplace_back(header, std::move(data));
    }
    d_->cv.notify_all();
}

void RelayHostLink::closeAfter(int conn, const core::json::Value& header) {
    {
        std::lock_guard<std::mutex> lock(d_->m);
        const auto it = d_->peers.find(conn);
        if (it == d_->peers.end() || it->second.closeWhenFlushed) return;
        auto& p = it->second;
        if (p.stage == Peer::Stage::Open) {
            p.frames.emplace_back(header, std::string{});
            p.frameBytes += kQueued;
            // Tout part, puis la connexion est fermee : sceller maintenant (la file est videe avant).
            while (!p.frames.empty()) {
                auto f = std::move(p.frames.front());
                p.frames.pop_front();
                p.sealFrame(f.first, f.second);
            }
            p.frameBytes = 0;
        }
        p.closeWhenFlushed = true;
        p.closeReason = header.str("raison");
    }
    d_->cv.notify_all();
}

void RelayHostLink::close(int conn) {
    {
        std::lock_guard<std::mutex> lock(d_->m);
        d_->drop(conn, "arr\xC3\xAAt", {});
    }
    d_->cv.notify_all();
}

std::size_t RelayHostLink::pending(int conn) const {
    std::lock_guard<std::mutex> lock(d_->m);
    const auto it = d_->peers.find(conn);
    return it == d_->peers.end() ? 0 : it->second.recordBytes + it->second.frameBytes;
}

// ========================================================================================== l'equipier ===
struct RelayGuestLink::Impl : Core {
    RelayGuestConfig cfg;
    std::string code, locator;
    Peer p;
    std::atomic<bool> hasConn{false};
    bool closedSaid{false};

    std::string base() const { return "/v1/relais/" + locator; }

    void end(const std::string& why) {   // sous le verrou
        if (closedSaid) return;
        closedSaid = true;
        dead = true;
        LinkEvent e;
        e.kind = LinkEvent::Kind::Closed;
        e.why = why;
        post(std::move(e));
        cv.notify_all();
    }

    // Un enregistrement de l'hote. Faux : fini (`why` ; un refus est deja dit).
    bool record(const std::string& rec, std::string& why) {
        if (rec[0] == 'C') {
            core::json::Value v;
            if (!core::json::parse(std::string_view(rec).substr(1), v) || !v.isObject()) {
                why = "message illisible de l'h\xC3\xB4te";
                return false;
            }
            const std::string o = v.str("op");
            if (o == "REFUS") {
                LinkEvent e;
                e.kind = LinkEvent::Kind::Frame;
                e.header = v;
                post(std::move(e));
                why = v.flag("code_faux") ? "code faux" : "refus\xC3\xA9";
                return false;
            }
            if (o == "CODE_REPONSE" && p.stage == Peer::Stage::Fresh) {
                const int left = static_cast<int>(std::clamp<std::int64_t>(v.integer("essais", 0), 0, 99));
                pairing::Tag got{};
                bool good = hexField(v, "Y", p.ya) && hexField(v, "confirmation", got)
                         && pairing::sessionKey(p.y, p.ya, p.ys, p.ya, p.sid, p.isk);
                const auto conv = conversation(locator, p.conn, p.sid, p.ys, p.ya);
                if (good) {
                    const pairing::Tag expected = pairing::confirmation(p.isk, conv, true);
                    good = crypto_verify_32(expected.data(), got.data()) == 0;
                }
                crypto::wipe(p.y);
                if (!good) {
                    // L'hote ne prouve pas le code : un code faux (ou quelqu'un s'interpose). Rien de plus ne part.
                    LinkEvent e;
                    e.kind = LinkEvent::Kind::Frame;
                    e.header = refusal(triesText(left) + " : v\xC3\xA9rifie le code donn\xC3\xA9 par l'h\xC3\xB4te", true, left);
                    post(std::move(e));
                    why = "code faux";
                    return false;
                }
                core::json::Value fin = op("CODE_FIN");
                fin.set("confirmation", crypto::hex(pairing::confirmation(p.isk, conv, false)));
                p.pushClear(fin);
                trafficKeys(p.isk, false, p.tx, p.rx);
                crypto::wipe(p.isk);
                p.stage = Peer::Stage::Keyed;
                core::json::Value hello = cfg.hello.isObject() ? cfg.hello : core::json::Value::object();
                hello.set("op", "BONJOUR");
                hello.set("proto", kProtocol);
                p.sealFrame(hello, {});
                return true;
            }
            return true;   // autre chose : ignore
        }
        if (rec[0] != 'S' || (p.stage != Peer::Stage::Keyed && p.stage != Peer::Stage::Open)) {
            why = "message inattendu de l'h\xC3\xB4te";
            return false;
        }
        std::string piece;
        bool more = false;
        switch (p.unseal(rec, piece, more)) {
            case Peer::Unsealed::Duplicate: return true;
            case Peer::Unsealed::Bad:
                why = "message refus\xC3\xA9 (modifi\xC3\xA9, perdu ou dans le d\xC3\xA9sordre)";
                return false;
            default: break;
        }
        core::json::Value h;
        std::string data;
        bool done = false;
        if (!p.takePiece(piece, more, done, h, data, why)) return false;
        if (!done) return true;
        if (p.stage == Peer::Stage::Keyed) {
            if (h.str("op") != "LIEN_OK") {
                why = "r\xC3\xA9ponse inattendue de l'h\xC3\xB4te";
                return false;
            }
            p.stage = Peer::Stage::Open;
            LinkEvent ok;
            ok.kind = LinkEvent::Kind::Connected;
            ok.address = "par Internet";
            ok.codeOk = h.flag("code");
            post(std::move(ok));
            return true;
        }
        if (h.str("op") == "PING") return true;
        LinkEvent e;
        e.kind = LinkEvent::Kind::Frame;
        e.header = std::move(h);
        e.data = std::move(data);
        post(std::move(e));
        return true;
    }

    void leave() {
        if (!hasConn) return;
        core::json::Value b = core::json::Value::object();
        b.set("conn", static_cast<std::int64_t>(p.conn));
        b.set("partir", true);
        (void)ask(*cfg.transport, "POST", base() + "/fermer", core::json::write(b, false));
    }

    void readLoop() {
        auto& t = *cfg.transport;
        // Le localisateur (Argon2id : ~0,2 s, ici et pas dans le fil de l'interface).
        {
            std::string loc = relayLocator(code);
            std::lock_guard<std::mutex> lock(m);
            if (loc.empty()) {
                end("la cl\xC3\xA9 de recherche de la session ne se calcule pas (m\xC3\xA9moire)");
                return;
            }
            locator = std::move(loc);
        }
        if (stopping) return;
        Answer a;
        for (int tries = 0;; ++tries) {
            a = ask(t, "POST", base() + "/frapper", "{}");
            if (stopping) return;
            if (a.status == 200 || !((a.status == 0 || a.status >= 500) && a.status != 503) || tries >= 2) break;
            sleepFor(1.0 + tries);
            if (stopping) return;
        }
        const auto c = a.json.integer("conn", 0);
        if (a.status != 200 || c <= 0 || c > 0x7FFFFFFF) {
            std::lock_guard<std::mutex> lock(m);
            end(a.status == 0 ? "le service XPG ne r\xC3\xA9pond pas (" + said(a) + ")" : serviceTooOld(a) ? relayTooOldText() : said(a));   // 1.45.0
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m);
            p.conn = static_cast<int>(c);
            p.host = false;
            p.locator = locator;
            p.since = now();
            LinkEvent e;
            e.kind = LinkEvent::Kind::Opened;
            e.header = core::json::Value::object();
            e.header.set("nom", a.json.str("nom"));
            e.header.set("hote", a.json.str("hote"));
            e.header.set("projet", a.json.str("projet"));
            post(std::move(e));
            // L'echange du code : notre part.
            pairing::Point g{};
            crypto::randomBytes(p.sid.data(), p.sid.size());
            if (!generator(code, p.sid, canalOf(locator, p.conn), g) || !pairing::newShare(g, p.y, p.ys)) {
                end("l'\xC3\xA9" "change du code ne se calcule pas");
                return;
            }
            core::json::Value h = op("CODE_DEBUT");
            h.set("sid", crypto::hex(p.sid));
            h.set("Y", crypto::hex(p.ys));
            p.pushClear(h);
            hasConn = true;
        }
        cv.notify_all();
        std::uint64_t since = 0;
        double failSince = 0, backoff = 1;
        bool left = false;
        while (!stopping && !dead) {
            int wait = kWaitSeconds;
            {
                std::lock_guard<std::mutex> lock(m);
                if (p.stage != Peer::Stage::Open) {
                    const double rest = kHandshakeSeconds - (now() - p.since);
                    if (rest <= 0) {
                        end("l'h\xC3\xB4te ne r\xC3\xA9pond pas (60 s)");
                        break;
                    }
                    wait = std::max(1, std::min(wait, static_cast<int>(rest) + 1));
                }
            }
            const Answer r = ask(t, "GET", base() + "/recevoir?depuis=" + std::to_string(since) + "&conn=" + std::to_string(p.conn) + "&attente="
                                              + std::to_string(wait),
                                 {}, wait);
            if (stopping) break;
            if (r.status == 200) {
                failSince = 0;
                backoff = 1;
                const auto& msgs = r.json.at("messages").items();
                bool over = false;
                {
                    std::lock_guard<std::mutex> lock(m);
                    for (const auto& msg : msgs) {
                        if (msg.str("de") != "hote") continue;
                        if (msg.flag("fin")) {
                            const std::string why = msg.str("raison");
                            end(why.empty() ? std::string("l'h\xC3\xB4te a ferm\xC3\xA9 la connexion") : why);
                            over = true;
                            break;
                        }
                        crypto::Bytes raw;
                        std::vector<std::string> recs;
                        if (!crypto::unbase64(msg.str("donnees"), raw)
                            || !relay::unpackRecords(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), recs)) {
                            end("donn\xC3\xA9" "es illisibles de l'h\xC3\xB4te");
                            over = left = true;
                            break;
                        }
                        for (const auto& rec : recs) {
                            std::string why;
                            if (!record(rec, why)) {
                                end(why);
                                over = left = true;
                                break;
                            }
                        }
                        if (over) break;
                    }
                }
                cv.notify_all();
                if (over) break;
                const auto next = r.json.integer("suivant", static_cast<std::int64_t>(since));
                if (next >= 0) since = static_cast<std::uint64_t>(next);
                if (!msgs.empty()) sleepFor(kReadGap);
                continue;
            }
            if (r.status == 404 || r.status == 410 || r.status == 403) {
                std::lock_guard<std::mutex> lock(m);
                const std::string why = r.json.str("raison");
                end(why.empty() ? "la session est ferm\xC3\xA9" "e (" + said(r) + ")" : why);
                break;
            }
            if (r.status == 429 || r.status == 503) {
                sleepFor(retryAfter(r, 2));
                continue;
            }
            if (failSince == 0) failSince = now();
            if (now() - failSince > kOfflineSeconds) {
                std::lock_guard<std::mutex> lock(m);
                end("plus de connexion au service XPG depuis 2 min (" + said(r) + ")");
                break;
            }
            sleepFor(backoff);
            backoff = std::min(backoff * 2, 10.0);
        }
        // Un code faux, un refus : ce PC quitte la connexion sur le relais (rien de plus ne part).
        if (left && !stopping) leave();
    }

    bool work() const {   // sous le verrou
        return !p.records.empty() || (p.stage == Peer::Stage::Open && !p.frames.empty()) || (p.closeWhenFlushed && p.records.empty());
    }

    void writeLoop() {
        auto& t = *cfg.transport;
        double lastSend = 0, retryAt = 0, backoff = 1;
        for (;;) {
            std::size_t count = 0;
            std::string donnees;
            bool exiting = false;
            {
                std::unique_lock<std::mutex> lock(m);
                cv.wait_for(lock, std::chrono::milliseconds(200),
                            [&] { return stopping.load() || closing.load() || dead.load() || (hasConn && now() >= retryAt && work()); });
                if (stopping) break;
                if (dead) {
                    finished = true;
                    cv.notify_all();
                    return;
                }
                if (!hasConn) {
                    if (closing) {
                        finished = true;
                        cv.notify_all();
                        return;
                    }
                    continue;
                }
                if (now() >= retryAt) {
                    p.sealQueued();
                    std::vector<std::string> part;
                    std::size_t bytes = 0;
                    for (const auto& r : p.records) {
                        if (!part.empty() && bytes + r.size() + 4 > relay::kBatch) break;
                        bytes += r.size() + 4;
                        part.push_back(r);
                    }
                    count = part.size();
                    if (count) donnees = relay::packRecords(part);
                }
                exiting = (closing || p.closeWhenFlushed) && count == 0 && p.records.empty() && (p.stage != Peer::Stage::Open || p.frames.empty());
            }
            if (exiting) {
                leave();
                std::lock_guard<std::mutex> lock(m);
                end("arr\xC3\xAAt");
                finished = true;
                cv.notify_all();
                return;
            }
            if (!count) continue;
            if (const double gap = now() - lastSend; gap < kSendGap) sleepFor(kSendGap - gap);
            if (stopping) break;
            core::json::Value body = core::json::Value::object();
            body.set("conn", static_cast<std::int64_t>(p.conn));
            body.set("a", "hote");
            body.set("donnees", crypto::base64(donnees.data(), donnees.size()));
            const Answer a = ask(t, "POST", base() + "/envoyer", core::json::write(body, false));
            lastSend = now();
            if (stopping) break;
            std::lock_guard<std::mutex> lock(m);
            if (a.status == 200) {
                backoff = 1;
                for (std::size_t i = 0; i < count && !p.records.empty(); ++i) {
                    p.recordBytes -= std::min(p.recordBytes, p.records.front().size());
                    p.records.pop_front();
                }
                cv.notify_all();
            } else if (a.status == 404 || a.status == 410) {
                end("la session est ferm\xC3\xA9" "e (" + said(a) + ")");
            } else if (a.status == 429 || a.status == 503) {
                retryAt = now() + retryAfter(a, 1);
            } else if (a.status == 0 || a.status >= 500) {
                retryAt = now() + backoff;
                backoff = std::min(backoff * 2, 10.0);
            } else {
                end("envoi refus\xC3\xA9 par le service XPG : " + said(a));
            }
        }
    }
};

RelayGuestLink::RelayGuestLink() : d_(std::make_shared<Impl>()) {}
RelayGuestLink::~RelayGuestLink() { stop(); }

bool RelayGuestLink::start(const RelayGuestConfig& cfg, std::string* why) {
    stop();
    const auto fail = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!cfg.transport) return fail("pas de service XPG pour les sessions par Internet");
    std::string code;
    if (!relayCodeTyped(cfg.code, code))
        return fail("ce n'est pas un code de session par Internet (10 caract\xC3\xA8res, par exemple K7Q4-M2XP-T9)");
    if (!crypto::init()) return fail("la cryptographie ne d\xC3\xA9marre pas");
    d_ = std::make_shared<Impl>();
    d_->cfg = cfg;
    d_->code = code;   // le localisateur : calcule dans le fil du lien (Argon2id)
    auto* d = d_.get();
    d_->reader = std::thread([d] { d->readLoop(); });
    d_->writer = std::thread([d] { d->writeLoop(); });
    return true;
}

void RelayGuestLink::stop() {
    if (!d_) return;
    d_->halt(d_->cfg.transport.get());
}

bool RelayGuestLink::running() const noexcept { return d_ && d_->reader.joinable() && !d_->dead && !d_->stopping; }

std::vector<LinkEvent> RelayGuestLink::poll() { return d_->take(); }

void RelayGuestLink::send(const core::json::Value& header, std::string data) {
    {
        std::lock_guard<std::mutex> lock(d_->m);
        auto& p = d_->p;
        if (p.closeWhenFlushed || d_->dead) return;
        p.frameBytes += data.size() + kQueued;
        p.frames.emplace_back(header, std::move(data));
    }
    d_->cv.notify_all();
}

void RelayGuestLink::closeAfter(const core::json::Value& header) {
    {
        std::lock_guard<std::mutex> lock(d_->m);
        auto& p = d_->p;
        if (p.closeWhenFlushed || d_->dead) return;
        if (p.stage == Peer::Stage::Open) {
            p.frames.emplace_back(header, std::string{});
            while (!p.frames.empty()) {
                auto f = std::move(p.frames.front());
                p.frames.pop_front();
                p.sealFrame(f.first, f.second);
            }
            p.frameBytes = 0;
        }
        p.closeWhenFlushed = true;
    }
    d_->cv.notify_all();
}

// ==================================================================== 1.45.0 : le service trop vieux ===
const std::string& relayTooOldText() {
    static const std::string t = "Le service des licences n'est pas encore en 1.44 : l'\xC3\xA9" "diteur doit le red\xC3\xA9ployer "
                                 "(Admin \xE2\x80\xBA Service en ligne \xE2\x80\xBA D\xC3\xA9ployer)";
    return t;
}

bool relayTooOld(std::string_view why) { return why.find(relayTooOldText()) != std::string_view::npos; }

} // namespace session
