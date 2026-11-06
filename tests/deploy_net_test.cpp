// =============================================================================
//  tests/deploy_net_test.cpp - 1.28.0 : le reseau du deploiement, de bout en bout
// -----------------------------------------------------------------------------
//  De vraies sockets sur 127.0.0.1 (ports choisis par le systeme), de vraies
//  poignees de main TLS 1.3, des dossiers temporaires : l'agent tourne dans ses
//  fils, le client (le cote Studio) lui parle.
//    - l'identite TLS (creee, relue), les trames (format, trop grosses), CPace
//      (G lie aux empreintes, point identite refuse), le limiteur UDP ;
//    - IDENTIFY, une version incompatible, un pair non appaire (STATUS refuse),
//      une operation inconnue ;
//    - l'appairage : sans code, code faux, un essai par connexion, 3 echecs ->
//      code efface, code expire, un tiers qui s'interpose (un relais TLS reel avec
//      ses propres certificats, et une empreinte imposee au calcul), le bon code
//      -> les deux gardent l'empreinte ; le code consomme ;
//    - STATUS, GET_LOGS (le format du journal), le rejeu (meme identifiant ->
//      meme reponse sans rien refaire, seq qui recule, ts trop vieux), une trame
//      de 1 Mio qui passe, une trame trop grosse refusee ;
//    - 8 connexions au plus, la decouverte UDP en direct, un port deja pris,
//      etat.json, UNPAIR, un certificat d'agent qui change -> refuse, l'arret.
//
//  deploy_net_test --distant ADRESSE PORT PORT_UDP CODE : l'essai croise (un agent
//  ailleurs, par exemple l'agent Windows sous Wine) : decouverte, IDENTIFY,
//  appairage, STATUS, GET_LOGS.
// =============================================================================
#include "../src/deploy/Agent.hpp"
#include "../src/deploy/Client.hpp"
#include "../src/deploy/Discovery.hpp"
#include "../src/deploy/Frame.hpp"
#include "../src/deploy/Journal.hpp"
#include "../src/deploy/Net.hpp"
#include "../src/deploy/Pairing.hpp"
#include "../src/deploy/Protocol.hpp"
#include "../src/deploy/Tls.hpp"
#include "../src/deploy/TlsIdentity.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace deploy;

int g_checks = 0, g_failures = 0;
const bool g_verbose = std::getenv("DEPLOY_NET_VERBOSE") != nullptr;   // chaque controle, reussi ou non
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_verbose) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::string tempDir(const std::string& tag) {
    unsigned char r[6];
    crypto::randomBytes(r, sizeof r);
    const fs::path p = fs::temp_directory_path() / ("xpg-deploynet-" + tag + "-" + crypto::hex(r, sizeof r));
    fs::create_directories(p);
    return p.string();
}

bool exists(const std::string& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

// Un code faux : le premier chiffre change.
std::string wrong(const std::string& code) {
    std::string w = code;
    w[0] = w[0] == '9' ? '0' : static_cast<char>(w[0] + 1);
    return w;
}

AgentConfig agentConfig(const std::string& dir) {
    AgentConfig c;
    c.dir = dir;
    c.bindAddress = "127.0.0.1";
    c.port = 0;
    c.discoveryPort = 0;
    c.machineName = "PC-ESSAI";
    c.idleTimeoutMs = 20000;
    c.stateIntervalMs = 300;
    return c;
}

// Un tiers sur le chemin : il accepte la connexion du Studio avec SON certificat, se connecte a
// l'agent avec SON certificat, et recopie les trames dans les deux sens.
struct Relay {
    TlsIdentity tiers;
    net::TcpSocket listener;
    int port{0};
    std::string host;
    int target{0};
    std::thread thread;
    std::atomic<int> relayed{0};

    bool start(const std::string& targetHost, int targetPort) {
        host = targetHost;
        target = targetPort;
        if (!TlsIdentity::generate("XPG-Agent PC-ESSAI", tiers, nullptr)) return false;
        if (!listener.listen("127.0.0.1", 0, nullptr)) return false;
        port = listener.localPort();
        thread = std::thread([this] { run(); });
        return true;
    }
    void run() {
        net::TcpSocket s = listener.accept(10000);
        if (!s.valid()) return;
        TlsStream in, out;
        if (!in.accept(std::move(s), tiers, 10000, nullptr)) return;
        if (!out.connect(host, target, tiers, 10000, nullptr)) return;
        for (;;) {
            Frame f, g;
            if (readFrame(in, f, 10000, 10000, nullptr) != FrameRead::Ok) break;
            if (!writeFrame(out, f.header, f.data, 10000, nullptr)) break;
            if (readFrame(out, g, 10000, 10000, nullptr) != FrameRead::Ok) break;
            if (!writeFrame(in, g.header, g.data, 10000, nullptr)) break;
            ++relayed;
        }
    }
    ~Relay() {
        if (thread.joinable()) thread.join();
    }
};

int countLinesWith(const std::vector<std::string>& lines, const std::string& needle) {
    int n = 0;
    for (const auto& l : lines)
        if (l.find(needle) != std::string::npos) ++n;
    return n;
}

// ------------------------------------------------------------------ unites --
void unitTests(const std::string& base) {
    // L'identite : creee, relue, la meme empreinte ; M-XXXX.
    {
        const std::string dir = base + "/id";
        TlsIdentity a, b;
        std::string why;
        {
            const bool passed = TlsIdentity::loadOrCreate(dir, "studio", false, "XPG-Studio PC-DEV", a, &why);
            check(passed, "identit\xC3\xA9 cr\xC3\xA9\xC3\xA9" "e [" + why + "]");
        }
        check(exists(dir + "/studio.cer") && exists(dir + "/studio.cle"), "identit\xC3\xA9 : studio.cer et studio.cle \xC3\xA9" "crits");
        {
            const bool passed = TlsIdentity::loadOrCreate(dir, "studio", false, "autre nom", b, &why) && a.fingerprint() == b.fingerprint();
            check(passed, "identit\xC3\xA9 relue : la m\xC3\xAAme empreinte [" + why + "]");
        }
        check(std::regex_match(a.machineId(), std::regex("M-[0-9A-F]{4}")), "identifiant lisible " + a.machineId());
        check(a.fingerprint() == crypto::sha256(a.certificateDer().data(), a.certificateDer().size()), "empreinte = SHA-256 du DER");
        fs::remove(dir + "/studio.cle");
        {
            const bool passed = !TlsIdentity::loadOrCreate(dir, "studio", false, "x", b, &why) && why.find("incompl") != std::string::npos;
            check(passed, "une cl\xC3\xA9 perdue n'est pas remplac\xC3\xA9" "e en silence [" + why + "]");
        }
    }
    // Les trames.
    {
        core::json::Value h = protocol::requestHeader("STATUS", "op-12345678", 7, 1000);
        std::string bytes;
        check(encodeFrame(h, "{\"a\":1}", bytes, nullptr), "trame encod\xC3\xA9" "e");
        const std::string hdr = core::json::write(h, false);
        const std::uint32_t total = 4 + static_cast<std::uint32_t>(hdr.size()) + 7;
        const auto byte = [&bytes](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])); };
        check(bytes.size() == total + 4 && byte(0) == (total >> 24) && byte(3) == (total & 0xFF) && byte(7) == hdr.size(),
              "trame : longueurs gros-boutistes");
        Frame f;
        check(decodeFrame(bytes, f, nullptr) && f.header.str("op") == "STATUS" && f.header.integer("seq") == 7 && f.data == "{\"a\":1}",
              "trame d\xC3\xA9" "cod\xC3\xA9" "e");
        std::string why;
        check(!encodeFrame(h, std::string(kMaxFrame, 'x'), bytes, &why) && why.find("trop grosse") != std::string::npos,
              "une trame de plus de 2 Mio n'est pas encod\xC3\xA9" "e");
        check(!decodeFrame(std::string("\x00\x00\x00\x08\x00\x00\x00\x09{}{}", 12), f, nullptr), "longueur d'en-t\xC3\xAAte incoh\xC3\xA9rente refus\xC3\xA9" "e");
        check(protocol::chooseVersion({1}, {1, 2}) == 1 && protocol::chooseVersion({1}, {2}) == 0 && protocol::chooseVersion({1, 2}, {2, 1}) == 2,
              "n\xC3\xA9gociation de version");
        protocol::ReplayGuard g;
        std::string code, msg;
        const auto now = protocol::nowMs();
        check(g.accept(1, now, now, code, msg) && g.accept(2, now, now, code, msg), "seq croissant accept\xC3\xA9");
        check(!g.accept(2, now, now, code, msg) && code == "rejeu", "seq r\xC3\xA9p\xC3\xA9t\xC3\xA9 refus\xC3\xA9");
        check(!g.accept(3, now - 6 * 60 * 1000, now, code, msg) && code == "horloge", "ts trop vieux refus\xC3\xA9");
        check(!g.accept(3, now + 6 * 60 * 1000, now, code, msg) && code == "horloge", "ts trop en avance refus\xC3\xA9");
    }
    // CPace : G depend du code et des deux empreintes ; le point identite est refuse.
    {
        pairing::Sid sid{};
        crypto::Digest fa{}, fb{}, fc{};
        fa[0] = 1;
        fb[0] = 2;
        fc[0] = 3;
        pairing::Point g1{}, g2{}, g3{}, g4{};
        check(pairing::generator("123456", sid, fa, fb, g1) && pairing::generator("123456", sid, fa, fc, g2)
                  && pairing::generator("123457", sid, fa, fb, g3) && pairing::generator("123456", sid, fc, fb, g4),
              "G calcul\xC3\xA9");
        check(g1 != g2 && g1 != g3 && g1 != g4, "G change avec le code et avec chaque empreinte");
        check(!pairing::generator("12345", sid, fa, fb, g1), "un code de 5 chiffres refus\xC3\xA9");
        pairing::Scalar y{};
        pairing::Point ys{}, zero{};
        pairing::SessionKey isk{};
        check(pairing::newShare(g1, y, ys), "y.G");
        check(!pairing::sessionKey(y, zero, ys, zero, sid, isk), "le point identit\xC3\xA9 est refus\xC3\xA9");
        // L'agent refuse un PAIR_START qui porte le point identite.
        pairing::AgentPairing ap("123456", fa, fb, "PC", "M-0000");
        core::json::Value start = core::json::Value::object(), reply;
        start.set("sid", crypto::hex(sid));
        start.set("Ys", crypto::hex(zero));
        start.set("name", "PC-DEV");
        start.set("publisherKey", std::string(64, 'a'));
        std::string why;
        {
            const bool passed = !ap.start(start, reply, &why) && why.find("refus") != std::string::npos;
            check(passed, "PAIR_START avec le point identit\xC3\xA9 : refus\xC3\xA9 [" + why + "]");
        }
        // Un aller-retour complet, sans reseau : les deux confirmations concordent.
        pairing::StudioPairing sp("482913", fa, fb, "PC-DEV", std::string(64, 'b'));
        pairing::AgentPairing ag("482913", fa, fb, "PC-ESSAI", "M-0102");
        core::json::Value s1, r1, f1;
        check(sp.start(s1, nullptr) && ag.start(s1, r1, nullptr) && sp.answer(r1, f1, nullptr) && ag.finish(f1, nullptr),
              "CPace : le m\xC3\xAAme code des deux c\xC3\xB4t\xC3\xA9s s'accorde");
        pairing::StudioPairing sp2("482913", fa, fb, "PC-DEV", std::string(64, 'b'));
        pairing::AgentPairing ag2("482914", fa, fb, "PC-ESSAI", "M-0102");
        core::json::Value s2, r2, f2;
        check(sp2.start(s2, nullptr) && ag2.start(s2, r2, nullptr) && !sp2.answer(r2, f2, nullptr),
              "CPace : un autre code ne s'accorde pas");
    }
    // Le limiteur de la decouverte.
    {
        discovery::RateLimiter lim;
        const auto t = std::chrono::steady_clock::now();
        check(lim.allow("10.0.0.1", "n1", t) && !lim.allow("10.0.0.1", "n1", t), "UDP : une seule r\xC3\xA9ponse par (envoyeur, nonce)");
        int ok = 1;
        for (int i = 2; i <= 8; ++i) ok += lim.allow("10.0.0.1", "n" + std::to_string(i), t) ? 1 : 0;
        check(ok == 5, "UDP : 5 r\xC3\xA9ponses par seconde au plus pour un envoyeur (" + std::to_string(ok) + ")");
        int total = 0;
        for (int i = 0; i < 40; ++i) total += lim.allow("10.0.1." + std::to_string(i), "x", t) ? 1 : 0;
        check(total <= 20, "UDP : 20 r\xC3\xA9ponses d'avance au plus en tout (" + std::to_string(total) + ")");
        check(lim.allow("10.0.0.1", "n9", t + std::chrono::milliseconds(1500)), "UDP : le d\xC3\xA9" "bit revient");
        std::string nonce;
        discovery::Announce a;
        a.machine = "PC";
        a.id = "M-1234";
        a.version = "1.0.0";
        a.protos = {1};
        a.port = 47861;
        a.fingerprint = std::string(64, 'c');
        discovery::Announce b;
        check(discovery::parseHere(discovery::hereMessage("abc", a), nonce, b) && nonce == "abc" && b.fingerprint == a.fingerprint
                  && b.port == 47861 && b.protos.size() == 1,
              "HERE : aller-retour");
        check(discovery::parseDiscover(discovery::discoverMessage("n0"), nonce) && nonce == "n0" && !discovery::parseDiscover("{\"xpgd\":2}", nonce),
              "DISCOVER : lu, et une autre version ignor\xC3\xA9" "e");
    }
}

// ------------------------------------------------------------ de bout en bout --
void endToEnd(const std::string& base) {
    const std::string agentDir = base + "/agent";
    const std::string studioDir = base + "/studio";
    std::string why;
    TlsIdentity me;
    {
        const bool passed = TlsIdentity::loadOrCreate(studioDir, "studio", false, "XPG-Studio PC-DEV", me, &why);
        check(passed, "identit\xC3\xA9 du Studio [" + why + "]");
    }
    const auto devKey = crypto::newSignKeys();
    const std::string keyHex = crypto::hex(devKey.pub);

    auto agent = std::make_unique<Agent>(agentConfig(agentDir));
    {
        const bool passed = agent->start(&why);
        check(passed, "l'agent d\xC3\xA9marre [" + why + "]");
    }
    const int port = agent->port();
    check(port > 0 && agent->discoveryPort() > 0, "l'agent \xC3\xA9" "coute (TCP " + std::to_string(port) + ", UDP " + std::to_string(agent->discoveryPort()) + ")");
    check(exists(agentDir + "/identite.cer") && exists(agentDir + "/identite.cle"), "l'identit\xC3\xA9 de l'agent est cr\xC3\xA9\xC3\xA9" "e");
    const std::string agentFp = agent->identity().fingerprintHex();
    {
        core::json::Value st;
        check(readAgentState(agentDir, st, &why) && st.flag("running") && st.integer("port") == port, "etat.json \xC3\xA9" "crit au d\xC3\xA9marrage");
        const auto first = st.integer("updatedAt");
        pause(800);
        check(readAgentState(agentDir, st, &why) && st.integer("updatedAt") > first, "etat.json r\xC3\xA9\xC3\xA9" "crit p\xC3\xA9riodiquement");
    }

    // ---- TLS des deux cotes, IDENTIFY ----
    Client c(me, "PC-DEV", keyHex);
    Outcome o = c.connect("127.0.0.1", port, 5000);
    check(o.ok, "le Studio se connecte (TLS) [" + o.message + "]");
    check(c.peerFingerprint() == agentFp, "le Studio voit l'empreinte du certificat de l'agent");
    check(c.stream().protocolName() == "TLSv1.3", "TLS 1.3 n\xC3\xA9goci\xC3\xA9 (" + c.stream().protocolName() + ", " + c.stream().cipherName() + ")");
    AgentInfo info;
    o = c.identify(info);
    check(o.ok && info.proto == 1 && info.machine == "PC-ESSAI" && info.id == agent->machineId() && info.version == "1.0.0" && !info.paired,
          "IDENTIFY : version 1, PC-ESSAI, " + info.id + " [" + o.message + "]");
    {
        Client old(me, "PC-DEV", keyHex);
        old.setOfferedVersions({2, 3});
        AgentInfo i2;
        o = old.connect("127.0.0.1", port, 5000);
        Outcome o2 = old.identify(i2);
        check(o.ok && !o2.ok && o2.error == "incompatible" && o2.message.find("version") != std::string::npos,
              "IDENTIFY : aucune version commune -> << incompatible >> [" + o2.message + "]");
        core::json::Value r;
        o2 = old.request("STATUS", core::json::Value::object(), r);
        check(!o2.ok && o2.error == "identifier-d-abord", "rien avant une version choisie [" + o2.error + "]");
    }

    // ---- non appaire ----
    AgentStatus st;
    o = c.status(st);
    check(!o.ok && o.error == "non-appaire", "non appair\xC3\xA9 : STATUS refus\xC3\xA9 [" + o.message + "]");
    std::vector<std::string> lines;
    o = c.logs(10, lines);
    check(!o.ok && o.error == "non-appaire", "non appair\xC3\xA9 : GET_LOGS refus\xC3\xA9");
    o = c.unpair();
    check(!o.ok && o.error == "non-appaire", "non appair\xC3\xA9 : UNPAIR refus\xC3\xA9");
    {
        core::json::Value r;
        o = c.request("EXECUTE", core::json::Value::object(), r);
        check(!o.ok && o.error == "op-inconnue", "une op\xC3\xA9ration inconnue est refus\xC3\xA9" "e [" + o.message + "]");
    }

    // ---- l'appairage ----
    PairedMachine m;
    o = c.pair("123456", m);
    check(!o.ok && o.error == "pas-de-code" && o.message.find("--appairer") != std::string::npos, "appairage sans code en cours : refus\xC3\xA9 [" + o.message + "]");
    pairing::CodeState cs;
    {
        const bool passed = pairing::createCode(agentDir, cs, &why);
        check(passed, "un code (comme --appairer) [" + why + "]");
    }
    for (int attempt = 1; attempt <= 3; ++attempt) {
        Client k(me, "PC-DEV", keyHex);
        o = k.connect("127.0.0.1", port, 5000);
        Outcome p = k.pair(wrong(cs.code), m);
        check(o.ok && !p.ok && p.error == "code-faux", "code faux, essai " + std::to_string(attempt) + " : refus\xC3\xA9 [" + p.message + "]");
        if (attempt == 1) {
            p = k.pair(cs.code, m);
            check(!p.ok && p.error == "appairage", "un seul essai par connexion [" + p.message + "]");
            pairing::CodeState now;
            check(pairing::loadCode(agentDir, now, nullptr) == pairing::CodeLoad::Ok && now.attempts == 1, "l'essai manqu\xC3\xA9 est compt\xC3\xA9");
        }
    }
    check(!exists(pairing::codePath(agentDir)), "3 \xC3\xA9" "checs : le code est effac\xC3\xA9");
    {
        Client k(me, "PC-DEV", keyHex);
        (void)k.connect("127.0.0.1", port, 5000);
        o = k.pair(cs.code, m);
        check(!o.ok && o.error == "pas-de-code", "apr\xC3\xA8s 3 \xC3\xA9" "checs, le bon code ne sert plus [" + o.message + "]");
    }
    check(pairing::createCode(agentDir, cs, &why, 1), "un code qui expire aussit\xC3\xB4t");
    pause(30);
    {
        Client k(me, "PC-DEV", keyHex);
        (void)k.connect("127.0.0.1", port, 5000);
        o = k.pair(cs.code, m);
        check(!o.ok && o.error == "pas-de-code" && o.message.find("expir") != std::string::npos, "code expir\xC3\xA9 : refus\xC3\xA9 [" + o.message + "]");
        check(!exists(pairing::codePath(agentDir)), "le code expir\xC3\xA9 est effac\xC3\xA9");
    }
    check(pairing::createCode(agentDir, cs, &why), "un nouveau code");
    {
        // Un calcul fait avec une autre empreinte d'agent (celle d'un tiers) : echoue.
        TlsIdentity other;
        check(TlsIdentity::generate("tiers", other, nullptr), "identit\xC3\xA9 d'un tiers");
        Client k(me, "PC-DEV", keyHex);
        (void)k.connect("127.0.0.1", port, 5000);
        k.forcePairingAgentFingerprint(other.fingerprint());
        o = k.pair(cs.code, m);
        check(!o.ok && o.error == "code-faux", "le bon code avec une autre empreinte d'agent : \xC3\xA9" "chec [" + o.message + "]");
    }
    {
        // Un vrai relais TLS au milieu, avec ses propres certificats : le bon code echoue quand meme.
        Relay relay;
        check(relay.start("127.0.0.1", port), "un tiers s'interpose (relais TLS)");
        Client k(me, "PC-DEV", keyHex);
        o = k.connect("127.0.0.1", relay.port, 5000);
        check(o.ok && k.peerFingerprint() == relay.tiers.fingerprintHex() && k.peerFingerprint() != agentFp,
              "le Studio voit l'empreinte du tiers, pas celle de l'agent");
        AgentInfo viaRelay;
        o = k.identify(viaRelay);
        check(o.ok && viaRelay.machine == "PC-ESSAI", "le tiers relaie IDENTIFY");
        o = k.pair(cs.code, m);
        check(!o.ok && o.error == "code-faux", "le bon code \xC3\xA0 travers un tiers : \xC3\xA9" "chec [" + o.message + "]");
        k.close();
        relay.listener.close();
        check(relay.relayed.load() >= 2, "le relais a bien transmis les trames (" + std::to_string(relay.relayed.load()) + ")");
    }
    {
        pairing::StudioStore studios(agentDir);
        check(studios.load(nullptr) && studios.size() == 0, "personne n'est appair\xC3\xA9 apr\xC3\xA8s ces \xC3\xA9" "checs");
        pairing::CodeState now;
        check(pairing::loadCode(agentDir, now, nullptr) == pairing::CodeLoad::Ok && now.attempts == 2, "le code reste (2 essais sur 3)");
    }
    o = c.pair(" " + cs.code.substr(0, 3) + " " + cs.code.substr(3) + " ", m);
    check(o.ok, "le bon code : appair\xC3\xA9 (3e essai) [" + o.message + "]");
    check(m.fingerprint == agentFp && m.id == agent->machineId() && m.name == "PC-ESSAI" && m.port == port && m.address == "127.0.0.1",
          "le Studio garde l'empreinte, le nom, l'identifiant, l'adresse et le port de l'agent");
    check(!exists(pairing::codePath(agentDir)), "le code est consomm\xC3\xA9");
    {
        pairing::StudioStore studios(agentDir);
        pairing::PairedStudio s;
        check(studios.load(nullptr) && studios.find(me.fingerprintHex(), s) && s.name == "PC-DEV" && s.publisherKey == keyHex,
              "l'agent garde l'empreinte, le nom et la cl\xC3\xA9 d'\xC3\xA9" "diteur du Studio");
    }
    MachineStore store(studioDir);
    {
        const bool passed = store.load(&why) && store.upsert(m, &why);
        check(passed, "le magasin des machines appair\xC3\xA9" "es [" + why + "]");
    }
    {
        MachineStore again(studioDir);
        PairedMachine back;
        check(again.load(&why) && again.find(agentFp, back) && back.port == port && back.name == "PC-ESSAI" && back.id == m.id,
              "machines-appairees.json relu");
    }
    {
        Client k(me, "PC-DEV", keyHex);
        (void)k.connect("127.0.0.1", port, 5000);
        o = k.pair(cs.code, m);
        check(!o.ok && o.error == "pas-de-code", "le code ne sert qu'une fois");
    }

    // ---- appaire : STATUS, GET_LOGS ----
    o = c.status(st);
    check(o.ok && st.machine == "PC-ESSAI" && st.id == agent->machineId() && st.version == "1.0.0" && !st.system.empty() && st.uptimeS >= 0
              && st.diskFree > 0 && st.pairedCount == 1 && st.apps.empty() && st.startedAt > 0,
          "STATUS : " + st.machine + ", " + st.system + ", " + std::to_string(st.diskFree / (1024 * 1024)) + " Mio libres [" + o.message + "]");
    {
        Client k(me, "PC-DEV", keyHex);
        o = k.connect(m, 5000);
        AgentStatus s2;
        Outcome o2 = k.status(s2);
        check(o.ok && o2.ok && s2.pairedCount == 1, "une nouvelle connexion \xC3\xA9pingl\xC3\xA9" "e : STATUS [" + o.message + o2.message + "]");
        core::json::Value args = core::json::Value::object(), r;
        args.set("pad", std::string(1024 * 1024, 'p'));
        o2 = k.request("STATUS", args, r);
        check(o2.ok && r.str("machine") == "PC-ESSAI", "une trame de 1 Mio passe [" + o2.message + "]");
    }
    o = c.logs(50, lines);
    check(o.ok && !lines.empty() && countLinesWith(lines, "PAIR_FINISH") >= 1 && countLinesWith(lines, "STATUS") >= 1,
          "GET_LOGS : " + std::to_string(lines.size()) + " lignes [" + o.message + "]");
    {
        bool format = !lines.empty();
        int withOp = 0;
        const std::regex lineRe("^[0-9]{2}:[0-9]{2}:[0-9]{2} (op-[0-9a-f]{8}|-) [A-Z_]+( .*)?$");
        for (const auto& l : lines) {
            format = format && std::regex_match(l, lineRe);
            if (std::regex_search(l, std::regex("^[0-9:]{8} op-[0-9a-f]{8} STATUS"))) ++withOp;
        }
        check(format && withOp >= 1, "le journal : HH:MM:SS op-xxxxxxxx OP d\xC3\xA9tail");
        check(Journal(agentDir).todayPath().find("/journal/") != std::string::npos && exists(Journal(agentDir).todayPath()),
              "journal/AAAA-MM-JJ.log");
        o = c.logs(3, lines);
        check(o.ok && lines.size() == 3, "GET_LOGS : les N derni\xC3\xA8res lignes");
    }

    // ---- le rejeu ----
    {
        const std::string id = "op-0000beef";
        Frame r1, r2, r3;
        o = c.exchange(protocol::requestHeader("STATUS", id, c.nextSeq(), protocol::nowMs()), "{}", r1);
        check(o.ok && r1.header.flag("ok") && !r1.header.flag("replay"), "STATUS (identifiant " + id + ")");
        pause(1100);   // un STATUS refait aurait un autre << uptimeS >>
        o = c.exchange(protocol::requestHeader("STATUS", id, c.nextSeq(), protocol::nowMs()), "{}", r2);
        check(o.ok && r2.header.flag("ok") && r2.header.flag("replay") && r2.data == r1.data,
              "m\xC3\xAAme identifiant : la m\xC3\xAAme r\xC3\xA9ponse, sans rien refaire");
        Client k(me, "PC-DEV", keyHex);
        AgentInfo i3;
        (void)k.connect(m, 5000);
        (void)k.identify(i3);
        o = k.exchange(protocol::requestHeader("STATUS", id, k.nextSeq(), protocol::nowMs()), "{}", r3);
        check(o.ok && r3.header.flag("replay") && r3.data == r1.data, "m\xC3\xAAme identifiant sur une autre connexion : la m\xC3\xAAme r\xC3\xA9ponse");
        std::vector<std::string> all;
        (void)c.logs(1000, all);
        check(countLinesWith(all, id) == 1, "l'op\xC3\xA9ration rejou\xC3\xA9" "e n'est pas refaite (une ligne au journal)");
        Frame r4, r5;
        o = c.exchange(protocol::requestHeader("STATUS", protocol::newOperationId(), 1, protocol::nowMs()), "{}", r4);
        check(o.ok && !r4.header.flag("ok") && r4.header.str("error") == "rejeu", "seq qui recule : refus\xC3\xA9 [" + r4.header.str("message") + "]");
        o = c.exchange(protocol::requestHeader("STATUS", protocol::newOperationId(), c.nextSeq(), protocol::nowMs() - 10 * 60 * 1000), "{}", r5);
        check(o.ok && !r5.header.flag("ok") && r5.header.str("error") == "horloge", "ts trop vieux : refus\xC3\xA9 [" + r5.header.str("message") + "]");
        o = c.status(st);
        check(o.ok, "la connexion sert encore apr\xC3\xA8s ces refus");
    }

    // ---- un autre Studio (ou le meme avec un autre certificat) : non appaire ----
    {
        TlsIdentity otherStudio;
        check(TlsIdentity::generate("XPG-Studio PC-DEV", otherStudio, nullptr), "un autre certificat de Studio");
        Client k(otherStudio, "PC-DEV", keyHex);
        o = k.connect(m, 5000);
        AgentStatus s3;
        Outcome o2 = k.status(s3);
        check(o.ok && !o2.ok && o2.error == "non-appaire", "un certificat de Studio qui change : l'agent refuse [" + o2.message + "]");
    }

    // ---- une trame trop grosse ----
    {
        Client k(me, "PC-DEV", keyHex);
        (void)k.connect(m, 5000);
        const unsigned char big[8] = {0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};   // 3 Mio annonces
        check(k.stream().write(big, sizeof big, 5000, nullptr) == TlsStream::Io::Ok, "une trame de 3 Mio annonc\xC3\xA9" "e");
        Frame r;
        const FrameRead rd = readFrame(k.stream(), r, 5000, 5000, nullptr);
        check(rd == FrameRead::Ok && !r.header.flag("ok") && r.header.str("error") == "trame-trop-grosse",
              "trame trop grosse : refus\xC3\xA9" "e [" + r.header.str("message") + "]");
        Frame after;
        const FrameRead rd2 = readFrame(k.stream(), after, 5000, 5000, nullptr);
        check(rd2 == FrameRead::Closed, "puis l'agent ferme la connexion");
        Client k2(me, "PC-DEV", keyHex);
        (void)k2.connect(m, 5000);
        core::json::Value args = core::json::Value::object(), res;
        args.set("pad", std::string(kMaxFrame, 'p'));
        o = k2.request("STATUS", args, res);
        check(!o.ok && o.error == "trame-trop-grosse" && k2.connected(), "le Studio n'envoie pas une trame de plus de 2 Mio");
    }

    // ---- 8 connexions au plus ----
    {
        for (int i = 0; i < 250 && agent->activeConnections() > 1; ++i) pause(20);   // les connexions des essais precedents se ferment
        std::vector<std::unique_ptr<Client>> many;
        int opened = 0;
        for (int i = 0; i < 7; ++i) {
            many.push_back(std::make_unique<Client>(me, "PC-DEV", keyHex));
            if (many.back()->connect(m, 5000).ok) ++opened;
        }
        check(opened == 7 && agent->activeConnections() == 8, "8 connexions ouvertes (" + std::to_string(agent->activeConnections()) + ")");
        Client ninth(me, "PC-DEV", keyHex);
        o = ninth.connect(m, 3000);
        check(!o.ok && o.error == "reseau", "la 9e est refus\xC3\xA9" "e [" + o.message + "]");
        many.clear();
        for (int i = 0; i < 100 && agent->activeConnections() > 1; ++i) pause(20);
        o = ninth.connect(m, 3000);
        check(o.ok, "une place lib\xC3\xA9r\xC3\xA9" "e : la connexion passe");
    }

    // ---- la decouverte UDP ----
    {
        discovery::Options opt;
        opt.port = agent->discoveryPort();
        opt.waitMs = 700;
        opt.broadcast = false;
        opt.direct = {"127.0.0.1", "127.0.0.1"};
        const auto found = discovery::discover(opt, &why);
        check(found.size() == 1, "d\xC3\xA9" "couverte en direct sur 127.0.0.1 : " + std::to_string(found.size()) + " agent [" + why + "]");
        if (!found.empty()) {
            const auto& f = found.front();
            check(f.address == "127.0.0.1" && f.port == port && f.fingerprint == agentFp && f.machine == "PC-ESSAI" && f.id == agent->machineId()
                      && f.version == "1.0.0" && f.protos == std::vector<int>{1},
                  "HERE : adresse, port, empreinte, nom, identifiant, version, protocoles");
        }
        opt.broadcast = true;
        opt.direct = {"127.0.0.1"};
        const auto found2 = discovery::discover(opt, &why);
        check(found2.size() == 1, "en diffusion et en direct : sans doublon (" + std::to_string(found2.size()) + ")");
        opt.port = 1;   // personne
        opt.broadcast = false;
        opt.waitMs = 300;
        check(discovery::discover(opt, &why).empty(), "personne sur ce port : liste vide");
    }

    // ---- un port deja pris ----
    {
        net::TcpSocket taken;
        check(taken.listen("127.0.0.1", 0, nullptr), "un port TCP occup\xC3\xA9");
        AgentConfig cfg = agentConfig(base + "/agent2");
        cfg.port = taken.localPort();
        Agent second(cfg);
        std::string w;
        {
            const bool passed = !second.start(&w) && !second.running() && w.find("TCP " + std::to_string(cfg.port)) != std::string::npos;
            check(passed, "port TCP d\xC3\xA9j\xC3\xA0 pris : l'agent le dit et n'\xC3\xA9" "coute pas [" + w + "]");
        }
        net::UdpSocket udpTaken;
        check(udpTaken.bind("127.0.0.1", 0, nullptr), "un port UDP occup\xC3\xA9");
        net::TcpSocket probe;
        check(probe.listen("127.0.0.1", 0, nullptr), "un port TCP libre");
        const int freePort = probe.localPort();
        probe.close();
        AgentConfig cfg2 = agentConfig(base + "/agent3");
        cfg2.port = freePort;
        cfg2.discoveryPort = udpTaken.localPort();
        Agent third(cfg2);
        {
            const bool passed = !third.start(&w) && w.find("UDP " + std::to_string(cfg2.discoveryPort)) != std::string::npos;
            check(passed, "port UDP d\xC3\xA9j\xC3\xA0 pris : l'agent le dit [" + w + "]");
        }
        net::TcpSocket knock;
        check(!knock.connect("127.0.0.1", freePort, 1000, nullptr), "et il n'\xC3\xA9" "coute pas non plus en TCP");
    }

    // ---- UNPAIR ----
    o = c.unpair();
    check(o.ok, "UNPAIR [" + o.message + "]");
    check(store.remove(agentFp, &why) && !store.find(agentFp, m), "le Studio retire la machine de son magasin");
    store.upsert(m, nullptr);   // gardee pour l'essai suivant (l'empreinte epinglee)
    o = c.status(st);
    check(!o.ok && o.error == "non-appaire", "apr\xC3\xA8s UNPAIR : STATUS refus\xC3\xA9");
    {
        pairing::StudioStore studios(agentDir);
        check(studios.load(nullptr) && studios.size() == 0, "l'agent a oubli\xC3\xA9 le Studio");
    }

    // ---- l'arret, puis un certificat d'agent qui change ----
    c.close();
    {
        Client idle(me, "PC-DEV", keyHex);
        (void)idle.connect(m, 5000);   // une connexion muette ne retient pas l'arret
        const auto t0 = std::chrono::steady_clock::now();
        agent->stop();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        check(!agent->running() && ms < 3000, "arr\xC3\xAAt propre en " + std::to_string(ms) + " ms");
        core::json::Value s;
        check(readAgentState(agentDir, s, nullptr) && !s.flag("running"), "etat.json : arr\xC3\xAAt\xC3\xA9");
    }
    agent.reset();
    fs::remove(agentDir + "/identite.cer");
    fs::remove(agentDir + "/identite.cle");
    agent = std::make_unique<Agent>(agentConfig(agentDir));
    check(agent->start(&why) && agent->identity().fingerprintHex() != agentFp, "l'agent r\xC3\xA9initialis\xC3\xA9 a un nouveau certificat");
    {
        PairedMachine pinned = m;
        pinned.port = agent->port();
        Client k(me, "PC-DEV", keyHex);
        o = k.connect(pinned, 5000);
        check(!o.ok && o.error == "empreinte" && o.message.find("plus la m\xC3\xAAme machine") != std::string::npos && !k.connected(),
              "un certificat qui change apr\xC3\xA8s l'appairage : refus\xC3\xA9 [" + o.message + "]");
    }
    agent->stop();
}

// ---- l'essai croise : un agent ailleurs (l'agent Windows sous Wine) ----
int remote(const std::string& host, int port, int udpPort, const std::string& code) {
    const std::string base = tempDir("distant");
    std::string why;
    TlsIdentity me;
    if (!TlsIdentity::loadOrCreate(base, "studio", false, "XPG-Studio PC-LINUX", me, &why)) {
        std::printf("identite : %s\n", why.c_str());
        return 1;
    }
    discovery::Options opt;
    opt.port = udpPort;
    opt.waitMs = 1500;
    opt.broadcast = false;
    opt.direct = {host};
    const auto found = discovery::discover(opt, &why);
    std::printf("decouverte : %zu agent(s)\n", found.size());
    for (const auto& f : found)
        std::printf("  %s:%d %s %s version %s empreinte %s\n", f.address.c_str(), f.port, f.machine.c_str(), f.id.c_str(), f.version.c_str(),
                    f.fingerprint.substr(0, 16).c_str());
    const auto key = crypto::newSignKeys();
    Client c(me, "PC-LINUX", crypto::hex(key.pub));
    Outcome o = c.connect(host, port, 10000);
    std::printf("connexion TLS : %s %s (%s, %s)\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), c.stream().protocolName().c_str(),
                c.stream().cipherName().c_str());
    if (!o) return 1;
    std::printf("empreinte de l'agent : %s\n", c.peerFingerprint().c_str());
    AgentInfo info;
    o = c.identify(info);
    std::printf("IDENTIFY : %s %s - %s %s version %s proto %d\n", o.ok ? "ok" : "ECHEC", o.message.c_str(), info.machine.c_str(), info.id.c_str(),
                info.version.c_str(), info.proto);
    if (!o) return 1;
    AgentStatus st;
    o = c.status(st);
    std::printf("STATUS avant appairage : %s (%s)\n", o.ok ? "accepte (ANORMAL)" : "refuse", o.error.c_str());
    if (o.ok) return 1;
    PairedMachine m;
    o = c.pair(code, m);
    std::printf("appairage : %s %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str());
    if (!o) return 1;
    std::printf("  garde : %s %s %s:%d empreinte %s\n", m.name.c_str(), m.id.c_str(), m.address.c_str(), m.port, m.fingerprint.substr(0, 16).c_str());
    Client k(me, "PC-LINUX", crypto::hex(key.pub));
    o = k.connect(m, 10000);
    std::printf("reconnexion epinglee : %s %s\n", o.ok ? "ok" : "ECHEC", o.message.c_str());
    if (!o) return 1;
    o = k.status(st);
    std::printf("STATUS : %s %s - %s %s version %s, systeme << %s >>, %lld s, %lld octets libres, %d PC appaire(s), %zu application(s)\n",
                o.ok ? "ok" : "ECHEC", o.message.c_str(), st.machine.c_str(), st.id.c_str(), st.version.c_str(), st.system.c_str(),
                static_cast<long long>(st.uptimeS), static_cast<long long>(st.diskFree), st.pairedCount, st.apps.size());
    if (!o) return 1;
    std::vector<std::string> lines;
    o = k.logs(20, lines);
    std::printf("GET_LOGS : %s, %zu lignes\n", o.ok ? "ok" : "ECHEC", lines.size());
    for (const auto& l : lines) std::printf("  | %s\n", l.c_str());
    std::error_code ec;
    fs::remove_all(base, ec);
    return o.ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (!crypto::init()) {
        std::printf("libsodium indisponible\n");
        return 1;
    }
    if (argc == 6 && std::strcmp(argv[1], "--distant") == 0) return remote(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), argv[5]);
    const std::string base = tempDir("essai");
    unitTests(base);
    endToEnd(base);
    std::error_code ec;
    fs::remove_all(base, ec);
    std::printf("deploy_net_test : %d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
