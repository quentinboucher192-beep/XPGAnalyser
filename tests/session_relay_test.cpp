// =============================================================================
//  tests/session_relay_test.cpp - 1.44.0 : LES SESSIONS PAR INTERNET (le relais
//  du service XPG, chiffre de bout en bout : session/SessionRelay)
// -----------------------------------------------------------------------------
//    - le code (10 caracteres, montre "K7Q4-M2XP-T9", tape avec souplesse), le
//      localisateur (SHA-256 du code, 16 octets), les enregistrements et les trames ;
//    - un hote et deux equipiers a travers un RELAIS EN MEMOIRE (le transport
//      injectable : la meme semantique que service/src/relais.js) : 3 codes faux
//      (refuses, puis refuse meme avec le bon), le bon code tape en minuscules,
//      ATTENTE, une trame de 1 Mio, une de 2,5 Mio (en morceaux), la discussion, un
//      envoi rejoue (ignore), un envoi modifie (la connexion fermee), un equipier
//      sur le PC de l'hote (la meme cle), le depart d'un equipier, l'arret de l'hote ; les octets passes par le relais ne contiennent
//      jamais le texte en clair (un marqueur connu, le code) ;
//    - LE MEME SCENARIO contre le VRAI service sur ce poste (tests/service_local.hpp :
//      workerd par wrangler dev ; le transport HTTP des Studios, signe par des cles
//      creees ici). Sans Node.js ni wrangler : la premiere partie, puis "SAUTE : ...".
//
//    session_relay_test [SOURCE]
// =============================================================================
#include "service_local.hpp"

#include "../src/app/SessionRelayHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/session/SessionRelay.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (std::getenv("SESSION_VERBOSE")) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

using namespace session;
namespace json = core::json;
namespace crypto = deploy::crypto;

const std::string kMarker = "MARQUEUR-EN-CLAIR-4242";

std::string bytesOf(const crypto::Bytes& b) { return std::string(reinterpret_cast<const char*>(b.data()), b.size()); }

json::Value op(const std::string& name) {
    json::Value v = json::Value::object();
    v.set("op", name);
    return v;
}

bool waitFor(const std::function<bool()>& cond, int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return cond();
}

// Ce qui est passe par le relais (les donnees decodees, et les chemins et corps des appels).
struct Seen {
    std::mutex m;
    std::vector<std::string> bytes;
    void add(std::string s) {
        std::lock_guard<std::mutex> lock(m);
        bytes.push_back(std::move(s));
    }
    std::vector<std::string> all() {
        std::lock_guard<std::mutex> lock(m);
        return bytes;
    }
};

// ====================================================================================== le relais en memoire ===
//  La semantique de service/src/relais.js, sans HTTP ni signature : chaque transport porte la cle de son PC.
class FakeRelay {
public:
    struct Msg {
        std::uint64_t n{0};
        json::Value   de;
        std::string   donnees, cle, raison;
        bool          fin{false};
    };
    struct Box {
        std::deque<Msg> msgs;
        std::uint64_t   next{1};
        void put(Msg m) {
            m.n = next++;
            msgs.push_back(std::move(m));
        }
    };
    struct Conn {
        std::string key;
        Box         box;
        bool        closed{false};
    };
    struct Session {
        std::string          hostKey, nom, hote, projet;
        Box                  hostBox;
        std::map<int, Conn>  conns;
        int                  nextConn{1};
        bool                 closed{false};
    };

    std::shared_ptr<Seen> seen = std::make_shared<Seen>();
    std::atomic<int> duplicateNext{0};   // le prochain envoi vers l'hote arrive deux fois
    std::atomic<int> corruptNext{0};     // le prochain envoi scelle vers l'hote a un octet change
    // Un relais HOSTILE (ou une collision de localisateurs) : tout localisateur mene a la session ouverte - un code
    // faux arrive alors jusqu'a l'hote (l'echange CPace le refuse ; 3 essais par cle de PC).
    std::atomic<bool> routeAll{false};

    RelayTransportPtr transport(const std::string& key);

    RelayReply handle(const std::string& key, const RelayCall& c, const std::atomic<bool>& cancelled) {
        seen->add(c.path + "\n" + c.body);
        std::unique_lock<std::mutex> lock(m_);
        const std::string prefix = "/v1/relais/";
        std::string path = c.path, query;
        if (const auto q = path.find('?'); q != std::string::npos) {
            query = path.substr(q + 1);
            path = path.substr(0, q);
        }
        json::Value b;
        if (!c.body.empty()) (void)json::parse(c.body, b);
        if (path == prefix + "ouvrir") {
            const std::string loc = b.str("locator");
            auto it = sessions_.find(loc);
            if (it != sessions_.end() && !it->second.closed && it->second.hostKey != key) return error(409, "pris", "Ce code est pris.");
            Session s;
            s.hostKey = key;
            s.nom = b.str("nom");
            s.hote = b.str("hote");
            s.projet = b.str("projet");
            sessions_[loc] = std::move(s);
            return ok("{\"ok\":true}");
        }
        const auto slash = path.find('/', prefix.size());
        if (path.rfind(prefix, 0) != 0 || slash == std::string::npos) return error(404, "route", "Route inconnue.");
        const std::string loc = path.substr(prefix.size(), slash - prefix.size());
        const std::string action = path.substr(slash + 1);
        auto sit = sessions_.find(loc);
        if (sit == sessions_.end() && routeAll)
            for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
                if (!it->second.closed) sit = it;
        if (sit == sessions_.end() || (sit->second.closed && action != "recevoir"))
            return error(404, "absente", "Aucune session ouverte avec ce code : v\xC3\xA9rifie-le.");
        Session& s = sit->second;
        if (action == "frapper") {
            const int id = s.nextConn++;
            s.conns[id].key = key;
            json::Value r = json::Value::object();
            r.set("conn", static_cast<std::int64_t>(id));
            r.set("nom", s.nom);
            r.set("hote", s.hote);
            r.set("projet", s.projet);
            return ok(json::write(r, false));
        }
        if (action == "envoyer") {
            if (b.has("conn")) {
                auto it = s.conns.find(static_cast<int>(b.integer("conn")));
                if (it == s.conns.end() || it->second.closed) return error(410, "fermee", "Connexion ferm\xC3\xA9" "e.");
                if (it->second.key != key) return error(403, "connexion", "Pas ta connexion.");
                std::string donnees = b.str("donnees");
                note(donnees);
                if (corruptNext > 0 && corrupt(donnees)) --corruptNext;
                Msg msg;
                msg.de = json::Value(static_cast<std::int64_t>(it->first));
                msg.cle = key;
                msg.donnees = donnees;
                s.hostBox.put(msg);
                if (duplicateNext > 0) {
                    --duplicateNext;
                    s.hostBox.put(msg);
                }
                cv_.notify_all();
                return ok("{\"ok\":true}");
            }
            if (key != s.hostKey) return error(403, "hote", "Pas l'h\xC3\xB4te.");
            json::Value unknown = json::Value::array();
            for (const auto& e : b.at("envois").items()) {
                const int a = static_cast<int>(e.integer("a"));
                auto it = s.conns.find(a);
                if (it == s.conns.end() || it->second.closed) {
                    unknown.push(static_cast<std::int64_t>(a));
                    continue;
                }
                note(e.str("donnees"));
                Msg msg;
                msg.de = json::Value("hote");
                msg.donnees = e.str("donnees");
                it->second.box.put(msg);
            }
            cv_.notify_all();
            json::Value r = json::Value::object();
            r.set("ok", true);
            r.set("refuses", json::Value::array());
            r.set("inconnues", std::move(unknown));
            return ok(json::write(r, false));
        }
        if (action == "recevoir") {
            const auto param = [&](const std::string& name, long long fallback) -> long long {
                const auto at = ("&" + query).find("&" + name + "=");
                if (at == std::string::npos) return fallback;
                return std::atoll(query.c_str() + at + name.size() + 1);
            };
            const long long depuis = param("depuis", 0), wait = param("attente", 25), conn = param("conn", -1);
            Box* box = nullptr;
            Conn* cn = nullptr;
            if (conn >= 0) {
                auto it = s.conns.find(static_cast<int>(conn));
                if (it == s.conns.end()) return error(410, "fermee", "Connexion ferm\xC3\xA9" "e.");
                if (it->second.key != key) return error(403, "connexion", "Pas ta connexion.");
                cn = &it->second;
                box = &cn->box;
            } else {
                if (key != s.hostKey) return error(403, "hote", "Pas l'h\xC3\xB4te.");
                if (s.closed) return error(410, "fermee", "Session ferm\xC3\xA9" "e.");
                box = &s.hostBox;
            }
            while (!box->msgs.empty() && box->msgs.front().n < static_cast<std::uint64_t>(depuis)) box->msgs.pop_front();
            const auto closedNow = [&] { return cn ? cn->closed : s.closed; };
            if (box->msgs.empty() && !closedNow())
                cv_.wait_for(lock, std::chrono::seconds(wait), [&] { return cancelled.load() || !box->msgs.empty() || closedNow(); });
            if (cancelled) return RelayReply{};
            if (box->msgs.empty() && closedNow()) return error(410, "fermee", "Ferm\xC3\xA9" "e.");
            json::Value list = json::Value::array();
            std::uint64_t next = static_cast<std::uint64_t>(depuis);
            for (const auto& msg : box->msgs) {
                json::Value x = json::Value::object();
                x.set("n", static_cast<std::int64_t>(msg.n));
                x.set("de", msg.de);
                if (!msg.donnees.empty()) x.set("donnees", msg.donnees);
                if (!msg.cle.empty()) x.set("cle", msg.cle);
                if (msg.fin) x.set("fin", true);
                if (!msg.raison.empty()) x.set("raison", msg.raison);
                list.push(std::move(x));
                next = msg.n + 1;
            }
            json::Value r = json::Value::object();
            r.set("messages", std::move(list));
            r.set("suivant", static_cast<std::int64_t>(std::max<std::uint64_t>(next, box->msgs.empty() ? box->next : next)));
            return ok(json::write(r, false));
        }
        if (action == "fermer") {
            if (b.has("conn") && b.flag("partir")) {
                auto it = s.conns.find(static_cast<int>(b.integer("conn")));
                if (it == s.conns.end()) return error(410, "fermee", "Ferm\xC3\xA9" "e.");
                Msg msg;
                msg.de = json::Value(static_cast<std::int64_t>(it->first));
                msg.cle = it->second.key;
                msg.fin = true;
                msg.raison = "parti";
                if (!it->second.closed) s.hostBox.put(msg);
                s.conns.erase(it);
                cv_.notify_all();
                return ok("{\"ok\":true}");
            }
            if (key != s.hostKey) return error(403, "hote", "Pas l'h\xC3\xB4te.");
            const auto closeConn = [&](Conn& cn, const std::string& why) {
                if (cn.closed) return;
                cn.closed = true;
                Msg msg;
                msg.de = json::Value("hote");
                msg.fin = true;
                msg.raison = why;
                cn.box.put(msg);
            };
            if (b.has("conn")) {
                auto it = s.conns.find(static_cast<int>(b.integer("conn")));
                if (it != s.conns.end()) closeConn(it->second, b.str("raison", "l'h\xC3\xB4te a ferm\xC3\xA9 la connexion"));
            } else {
                for (auto& [id, cn] : s.conns) {
                    (void)id;
                    closeConn(cn, "l'h\xC3\xB4te a arr\xC3\xAAt\xC3\xA9 la session");
                }
                s.closed = true;
            }
            cv_.notify_all();
            return ok("{\"ok\":true}");
        }
        return error(404, "route", "Route inconnue.");
    }

    void wake() { cv_.notify_all(); }

private:
    static RelayReply ok(std::string body) {
        RelayReply r;
        r.status = 200;
        r.body = std::move(body);
        return r;
    }
    static RelayReply error(int status, const std::string& code, const std::string& message) {
        json::Value v = json::Value::object();
        v.set("erreur", code);
        v.set("message", message);
        RelayReply r;
        r.status = status;
        r.body = json::write(v, false);
        return r;
    }
    void note(const std::string& b64) {
        crypto::Bytes b;
        if (crypto::unbase64(b64, b)) seen->add(bytesOf(b));
    }
    // Un octet change dans le premier enregistrement scelle.
    static bool corrupt(std::string& b64) {
        crypto::Bytes b;
        std::vector<std::string> recs;
        if (!crypto::unbase64(b64, b) || !relay::unpackRecords(bytesOf(b), recs)) return false;
        for (auto& r : recs)
            if (!r.empty() && r[0] == 'S') {
                r[r.size() - 3] = static_cast<char>(r[r.size() - 3] ^ 0x40);
                const std::string packed = relay::packRecords(recs);
                b64 = crypto::base64(packed.data(), packed.size());
                return true;
            }
        return false;
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::map<std::string, Session> sessions_;
};

class FakeTransport final : public RelayTransport {
public:
    FakeTransport(FakeRelay& r, std::string key) : relay_(r), key_(std::move(key)) {}
    RelayReply call(const RelayCall& c) override {
        if (cancelled_) return RelayReply{};
        return relay_.handle(key_, c, cancelled_);
    }
    void cancel() override {
        cancelled_ = true;
        relay_.wake();
    }

private:
    FakeRelay& relay_;
    std::string key_;
    std::atomic<bool> cancelled_{false};
};

RelayTransportPtr FakeRelay::transport(const std::string& key) { return std::make_shared<FakeTransport>(*this, key); }

// Un transport qui note ce qui passe (le vrai service) : les chemins, les corps, les donnees decodees.
class Recording final : public RelayTransport {
public:
    Recording(RelayTransportPtr inner, std::shared_ptr<Seen> seen) : inner_(std::move(inner)), seen_(std::move(seen)) {}
    RelayReply call(const RelayCall& c) override {
        seen_->add(c.path + "\n" + c.body);
        json::Value b;
        if (!c.body.empty() && json::parse(c.body, b)) {
            note(b.str("donnees"));
            for (const auto& e : b.at("envois").items()) note(e.str("donnees"));
        }
        RelayReply r = inner_->call(c);
        json::Value v;
        if (r.status == 200 && json::parse(r.body, v))
            for (const auto& m : v.at("messages").items()) note(m.str("donnees"));
        return r;
    }
    void cancel() override { inner_->cancel(); }

private:
    void note(const std::string& b64) {
        crypto::Bytes b;
        if (!b64.empty() && crypto::unbase64(b64, b)) seen_->add(bytesOf(b));
    }
    RelayTransportPtr inner_;
    std::shared_ptr<Seen> seen_;
};

// ================================================================================================= le code ===
void code() {
    std::printf("-- le code, le localisateur, les enregistrements\n");
    const std::string c = newRelayCode();
    check(c.size() == 10 && c.find_first_not_of("0123456789ABCDEFGHJKMNPQRSTVWXYZ") == std::string::npos, "un code : 10 caracteres de Crockford (" + c + ")");
    check(newRelayCode() != c, "deux codes differents");
    check(relayCodeShown("K7Q4M2XPT9") == "K7Q4-M2XP-T9", "montre K7Q4-M2XP-T9");
    std::string t;
    check(relayCodeTyped(" k7q4-m2xp-t9 ", t) && t == "K7Q4M2XPT9", "tape en minuscules, avec tirets et espaces");
    check(relayCodeTyped("o1il-abcd-ef", t) && t == "0111ABCDEF", "O pour 0, I et L pour 1");
    check(!relayCodeTyped("K7Q4-M2XP-T", t) && !relayCodeTyped("K7Q4-M2XP-T9X", t) && !relayCodeTyped("K7Q4-M2XP-TU", t),
          "trop court, trop long, une lettre hors de l'alphabet (U) : refuses");
    const auto d = crypto::sha256(std::string("K7Q4M2XPT9"));
    check(relayLocator("k7q4-m2xp-t9") == crypto::hex(d.data(), 16), "le localisateur : les 16 premiers octets de SHA-256(code)");
    check(relayLocator("K7Q4M2XPT9").size() == 32, "32 hexadecimaux");
    std::vector<std::string> recs{"Cabc", std::string(70000, 'x'), "S1"}, back;
    check(relay::unpackRecords(relay::packRecords(recs), back) && back == recs, "les enregistrements : aller-retour");
    check(!relay::unpackRecords(std::string("\0\0\0\x09" "abc", 7), back), "un enregistrement tronque : refuse");
    json::Value h = op("VUE");
    h.set("vue", 12);
    json::Value h2;
    std::string data;
    check(relay::decodeFrame(relay::encodeFrame(h, std::string("\0\x01z", 3)), h2, data) && h2.str("op") == "VUE" && data == std::string("\0\x01z", 3),
          "une trame : aller-retour (des donnees binaires)");
}

// ============================================================================================== le scenario ===
struct Net {
    std::string label;
    std::function<RelayTransportPtr(const std::string& who)> make;
    std::shared_ptr<Seen> seen;
    FakeRelay* fake{nullptr};
};

struct Events {
    std::vector<LinkEvent> all;
    template <class L>
    void pull(L& link) {
        for (auto& e : link.poll()) all.push_back(std::move(e));
    }
    const LinkEvent* find(LinkEvent::Kind k, const std::string& opName = {}, int conn = -1) const {
        for (const auto& e : all)
            if (e.kind == k && (opName.empty() || e.header.str("op") == opName) && (conn < 0 || e.conn == conn)) return &e;
        return nullptr;
    }
};

json::Value hello(const std::string& name) {
    json::Value h = json::Value::object();
    h.set("nom", name);
    h.set("pc", "PC-ESSAI");
    h.set("id_pc", "M-0001");
    h.set("logiciel", "IHM Studio");
    h.set("version", "1.44.0");
    return h;
}

void scenario(const Net& net) {
    const std::string L = " [" + net.label + "]";
    const std::string code = newRelayCode();
    std::string wrong = code;
    wrong[9] = wrong[9] == 'A' ? 'B' : 'A';

    RelayHostLink host;
    RelayHostConfig hc;
    hc.transport = net.make("hote");
    hc.code = code;
    hc.sessionName = "Mise en service Armoire_Gaz";
    hc.hostName = "Camille";
    hc.project = "Armoire_Gaz";
    std::string why;
    check(host.start(hc, &why), "l'hote demarre" + L + " (" + why + ")");
    Events he;
    check(waitFor([&] { he.pull(host); return he.find(LinkEvent::Kind::Opened) || he.find(LinkEvent::Kind::Closed); }, 15000) && host.opened(),
          "la session est ouverte sur le relais" + L);

    // ---- Zoe : 3 codes faux, puis le bon : refusee ----
    const auto attempt = [&](const std::string& c, Events& got) {
        RelayGuestLink g;
        RelayGuestConfig gc;
        gc.transport = net.make("zoe");
        gc.code = c;
        gc.hello = hello("Zo\xC3\xA9");
        std::string w;
        (void)g.start(gc, &w);
        got.all.clear();
        (void)waitFor([&] { got.pull(g); return got.find(LinkEvent::Kind::Closed) || got.find(LinkEvent::Kind::Connected); }, 20000);
        g.stop();
    };
    {
        // Un code faux : le relais ne connait pas son localisateur (SHA-256 du code) - aucune session.
        Events z;
        attempt(wrong, z);
        const auto* c = z.find(LinkEvent::Kind::Closed);
        check(c && c->why.find("aucune session") != std::string::npos && !z.find(LinkEvent::Kind::Opened) && !z.find(LinkEvent::Kind::Connected),
              "un code faux : le relais ne connait pas cette session" + L + (c ? " - " + c->why : std::string(" - rien")));
    }
    if (net.fake) {
        // Un relais hostile (qui mene un code faux jusqu'a l'hote) : l'echange CPace refuse, 3 essais par cle de PC.
        net.fake->routeAll = true;
        for (int i = 0; i < 3; ++i) {
            Events z;
            attempt(wrong, z);
            const auto* r = z.find(LinkEvent::Kind::Frame, "REFUS");
            check(r && r->header.flag("code_faux") && r->header.integer("essais", -1) == 2 - i && !z.find(LinkEvent::Kind::Connected),
                  "relais hostile, un code faux (" + std::to_string(i + 1) + ") : refuse par l'echange, encore " + std::to_string(2 - i) + " essai(s)" + L
                      + (r ? " - " + r->header.str("raison") : std::string(" - rien")));
        }
        {
            Events z;
            attempt(code, z);
            const auto* r = z.find(LinkEvent::Kind::Frame, "REFUS");
            check(r && !z.find(LinkEvent::Kind::Connected), "apres 3 codes faux depuis ce PC : refuse, meme avec le bon" + L);
        }
        net.fake->routeAll = false;
    }
    {
        Events z;
        RelayGuestLink g;
        RelayGuestConfig gc;
        gc.transport = net.make("zoe");
        gc.code = "pas un code";
        check(!g.start(gc, &why), "un texte qui n'est pas un code : refuse tout de suite" + L);
    }
    he.pull(host);
    check(!he.find(LinkEvent::Kind::Knock), "les codes faux : l'hote ne voit personne frapper" + L);

    // ---- Lea : le bon code, tape en minuscules ----
    std::string typed = relayCodeShown(code);
    for (char& ch : typed)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    RelayGuestLink lea;
    RelayGuestConfig lc;
    lc.transport = net.make("lea");
    lc.code = typed;
    lc.hello = hello("L\xC3\xA9" "a " + kMarker);
    check(lea.start(lc, &why), "Lea frappe" + L);
    Events le;
    check(waitFor([&] { le.pull(lea); return le.find(LinkEvent::Kind::Opened) != nullptr; }, 15000), "Lea : le relais connait la session" + L);
    if (const auto* o = le.find(LinkEvent::Kind::Opened))
        check(o->header.str("nom") == "Mise en service Armoire_Gaz" && o->header.str("hote") == "Camille", "son nom et son hote" + L);
    check(waitFor([&] { he.pull(host); return he.find(LinkEvent::Kind::Knock) != nullptr; }, 15000), "l'hote voit Lea frapper" + L);
    int conn = 0;
    if (const auto* k = he.find(LinkEvent::Kind::Knock)) {
        check(k->codeOk && k->header.str("nom") == "L\xC3\xA9" "a " + kMarker && k->header.str("op") == "BONJOUR" && k->address == "par Internet",
              "Knock : le code prouve, le BONJOUR dechiffre" + L);
        conn = k->conn;
    }
    host.send(conn, op("ATTENTE"));
    check(waitFor([&] { le.pull(lea); return le.find(LinkEvent::Kind::Frame, "ATTENTE") != nullptr; }, 15000), "Lea : connectee, puis ATTENTE" + L);
    check(le.find(LinkEvent::Kind::Connected) && le.find(LinkEvent::Kind::Connected)->codeOk, "Connected (le code)" + L);

    // ---- une trame de 1 Mio, une de 2,5 Mio (en morceaux) ----
    std::string mio;
    while (mio.size() < 1024u * 1024u) mio += kMarker + "-" + std::to_string(mio.size()) + ";";
    mio.resize(1024u * 1024u);
    json::Value pj = op("PROJET");
    pj.set("chemin", "ihm/" + kMarker + ".txt");
    host.send(conn, pj, mio);
    std::string big(2621440u, '\0');
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>((i * 131u) & 0xFF);
    host.send(conn, op("VUE"), big);
    check(waitFor([&] { le.pull(lea); return le.find(LinkEvent::Kind::Frame, "VUE") != nullptr; }, 60000), "Lea recoit 1 Mio puis 2,5 Mio" + L);
    if (const auto* p = le.find(LinkEvent::Kind::Frame, "PROJET")) check(p->data == mio && p->header.str("chemin") == "ihm/" + kMarker + ".txt", "1 Mio : intact" + L);
    else check(false, "1 Mio : arrive" + L);
    if (const auto* v = le.find(LinkEvent::Kind::Frame, "VUE")) check(v->data == big, "2,5 Mio : intact (reassemble)" + L);

    // ---- la discussion ----
    json::Value msg = op("MESSAGE");
    msg.set("texte", "bonjour " + kMarker);
    lea.send(msg);
    check(waitFor([&] { he.pull(host); return he.find(LinkEvent::Kind::Frame, "MESSAGE", conn) != nullptr; }, 15000), "l'hote recoit le message de Lea" + L);
    if (const auto* m = he.find(LinkEvent::Kind::Frame, "MESSAGE", conn)) check(m->header.str("texte") == "bonjour " + kMarker, "le message, de la bonne connexion" + L);

    // ---- Marc ----
    RelayGuestLink marc;
    RelayGuestConfig mc;
    mc.transport = net.make("hote");   // Marc sur le PC de l'hote : la meme cle (deux Studios cote a cote)
    mc.code = code;
    mc.hello = hello("Marc");
    check(marc.start(mc, &why), "Marc frappe (depuis le PC de l'hote : la meme cle)" + L);
    int conn2 = 0;
    check(waitFor([&] {
              he.pull(host);
              for (const auto& e : he.all)
                  if (e.kind == LinkEvent::Kind::Knock && e.conn != conn) conn2 = e.conn;
              return conn2 != 0;
          }, 15000),
          "l'hote voit Marc frapper (une autre connexion)" + L);
    host.send(conn2, op("ATTENTE"));
    Events me;
    check(waitFor([&] { me.pull(marc); return me.find(LinkEvent::Kind::Frame, "ATTENTE") != nullptr; }, 15000), "Marc : connecte" + L);
    json::Value all = op("MESSAGE");
    all.set("texte", "a tous");
    host.send(conn, all);
    host.send(conn2, all);
    check(waitFor([&] {
              le.pull(lea);
              me.pull(marc);
              int n = 0;
              for (const auto& e : le.all) n += e.header.str("texte") == "a tous";
              for (const auto& e : me.all) n += e.header.str("texte") == "a tous";
              return n == 2;
          }, 15000),
          "un message de l'hote a chacun" + L);

    if (net.fake) {
        // ---- un envoi rejoue : ignore ; un envoi modifie : la connexion fermee ----
        net.fake->duplicateNext = 1;
        json::Value un = op("MESSAGE");
        un.set("texte", "un");
        lea.send(un);
        json::Value deux = op("MESSAGE");
        deux.set("texte", "deux");
        lea.send(deux);
        check(waitFor([&] { he.pull(host); for (const auto& e : he.all) if (e.header.str("texte") == "deux") return true; return false; }, 15000),
              "apres un envoi rejoue, la suite passe" + L);
        int ones = 0;
        for (const auto& e : he.all) ones += e.kind == LinkEvent::Kind::Frame && e.header.str("texte") == "un";
        check(ones == 1, "un envoi rejoue : la trame n'arrive qu'une fois" + L);

        RelayGuestLink nina;
        RelayGuestConfig nc;
        nc.transport = net.make("nina");
        nc.code = code;
        nc.hello = hello("Nina");
        (void)nina.start(nc, &why);
        int conn3 = 0;
        (void)waitFor([&] {
            he.pull(host);
            for (const auto& e : he.all)
                if (e.kind == LinkEvent::Kind::Knock && e.conn != conn && e.conn != conn2) conn3 = e.conn;
            return conn3 != 0;
        }, 15000);
        Events ne;
        host.send(conn3, op("ATTENTE"));
        (void)waitFor([&] { ne.pull(nina); return ne.find(LinkEvent::Kind::Frame, "ATTENTE") != nullptr; }, 15000);
        net.fake->corruptNext = 1;
        json::Value x = op("MESSAGE");
        x.set("texte", "modifie");
        nina.send(x);
        check(waitFor([&] { he.pull(host); return he.find(LinkEvent::Kind::Closed, {}, conn3) != nullptr; }, 15000),
              "un envoi modifie en route : l'hote ferme cette connexion" + L);
        bool passed = false;
        for (const auto& e : he.all) passed = passed || e.header.str("texte") == "modifie";
        check(!passed, "le message modifie n'est pas rendu" + L);
        check(waitFor([&] { ne.pull(nina); return ne.find(LinkEvent::Kind::Closed) != nullptr; }, 15000), "Nina le voit (renvoyee)" + L);
        nina.stop();
    }

    // ---- Lea part : l'hote le voit ----
    json::Value bye = op("AUREVOIR");
    lea.closeAfter(bye);
    lea.stop();
    check(waitFor([&] { he.pull(host); return he.find(LinkEvent::Kind::Closed, {}, conn) != nullptr; }, 15000), "l'hote voit Lea partir" + L);
    check(he.find(LinkEvent::Kind::Frame, "AUREVOIR", conn) != nullptr, "son AUREVOIR est arrive avant" + L);

    // ---- l'hote arrete : Marc le voit ----
    json::Value end = op("AUREVOIR");
    end.set("fin", true);
    end.set("raison", "Camille a arr\xC3\xAAt\xC3\xA9 la session");
    host.closeAfter(conn2, end);
    host.stop();
    check(!host.running(), "l'hote s'arrete" + L);
    check(waitFor([&] { me.pull(marc); return me.find(LinkEvent::Kind::Closed) != nullptr; }, 15000), "Marc voit la session finir" + L);
    check(me.find(LinkEvent::Kind::Frame, "AUREVOIR") != nullptr, "Marc a recu l'AUREVOIR de l'hote" + L);
    marc.stop();

    // ---- rien en clair sur le relais ----
    const auto seen = net.seen->all();
    std::size_t total = 0;
    bool marker = false, codeSeen = false;
    for (const auto& s : seen) {
        total += s.size();
        marker = marker || s.find(kMarker) != std::string::npos;
        codeSeen = codeSeen || s.find(code) != std::string::npos || s.find(relayCodeShown(code)) != std::string::npos || s.find(typed) != std::string::npos;
    }
    check(total > 3u * 1024u * 1024u, "le relais a vu passer les octets (" + std::to_string(total) + ")" + L);
    check(!marker, "le relais n'a jamais vu le texte en clair (le marqueur : nom, message, projet)" + L);
    check(!codeSeen, "le relais n'a jamais vu le code" + L);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (!crypto::init()) {
        std::printf("ECHEC : libsodium\n");
        return 1;
    }
    code();

    std::printf("-- un hote et ses equipiers, par un relais en memoire\n");
    {
        FakeRelay relay;
        Net net;
        net.label = "relais en memoire";
        net.fake = &relay;
        net.seen = relay.seen;
        net.make = [&relay](const std::string& who) { return relay.transport(crypto::sha256Hex("cle d'essai " + who)); };
        scenario(net);
    }

    std::printf("-- le meme, par le vrai service sur ce poste\n");
    xpg_test::LocalService::Options so;
    if (argc > 1) so.sourceDir = argv[1];
    xpg_test::LocalService svc({}, so);
    if (svc.skipped() && g_failures == 0) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 0;
    }
    if (!svc.skipped()) {
        check(svc.ready(), "le service demarre (" + svc.error() + ")");
        if (svc.ready()) {
            std::map<std::string, std::shared_ptr<crypto::SignKeys>> keys;
            Net net;
            net.label = "service local";
            net.seen = std::make_shared<Seen>();
            const std::string url = svc.url();
            net.make = [&keys, &net, url](const std::string& who) {
                auto& k = keys[who];
                if (!k) k = std::make_shared<crypto::SignKeys>(crypto::newSignKeys());
                app::sessionrelay::HttpOptions o;
                o.baseUrl = url;
                o.software = "IHM-Studio/1.44.0";
                const auto kk = k;
                o.publicKey = [kk](crypto::PublicKey& pk, std::string&) {
                    pk = kk->pub;
                    return true;
                };
                o.sign = [kk](std::string_view m, crypto::Signature& sig, std::string&) {
                    sig = crypto::sign(kk->secret, m);
                    return true;
                };
                return std::make_shared<Recording>(app::sessionrelay::httpTransport(o), net.seen);
            };
            scenario(net);
        }
    }
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
