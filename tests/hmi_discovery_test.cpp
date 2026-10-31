// =============================================================================
//  tests/hmi_discovery_test.cpp - 1.21.0 : la decouverte reseau, sans intrusion
// -----------------------------------------------------------------------------
//  AUCUN PAQUET ne part : le reseau est un faux (FakeProber) - des appareils
//  inventes, chacun ses ports, sa MAC, son identification Modbus - et le temps
//  aussi (FakeClock : la cadence s'y mesure sans attendre). On verifie :
//    - les plages et leurs bornes (4096 adresses au plus), le masque,
//      l'interface, les protocoles, la cadence et les fils ;
//    - la cadence (Pacer) : jamais plus de N interrogations par seconde, un ou
//      plusieurs fils ; Arreter pendant l'attente ;
//    - la confirmation : sans accord, ou pour d'autres reglages, ou un accord
//      deja servi, le scan refuse - et rien n'est envoye ;
//    - la lecture seule : la trame d'identification (43 / 14) passe la liste
//      blanche, les ecritures (5, 6, 15, 16...), les lectures de mots, les
//      autres MEI non ; chaque trame partie pendant un scan simule est une
//      lecture d'identification ;
//    - un scan simule : accessible, inaccessible, identification partielle,
//      reponse inattendue, l'esclave 1 apres le 255, la base apres la courante ;
//      Arreter ;
//    - la fiche : confirme, annonce, suppose, non identifie ; jamais un modele
//      invente ;
//    - la comparaison avec un projet : declare, absent, injoignable, autre
//      adresse, doublons, incoherences ; les propositions ; le projet intact ;
//    - le journal : lance puis termine, relu du fichier.
// =============================================================================
#include "../src/hmi/HmiDiscovery.hpp"
#include "../src/hmi/HmiEquipment.hpp"
#include "../src/hmi/HmiIpScan.hpp"
#include "../src/hmi/HmiModbus.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiNetInfo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace ipscan = hmi::ipscan;
namespace disc = hmi::discovery;
namespace modbus = hmi::modbus;
using disc::Certainty;
using PingResult = hmi::netinfo::PingResult;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

std::uint32_t ip(const char* text) {
    std::uint32_t v = 0;
    (void)hmi::equip::parseIpv4(text, v);
    return v;
}

// ------------------------------------------------------------- le faux temps ---
class FakeClock final : public ipscan::Clock {
public:
    double now() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return t_;
    }
    void sleepUntil(double t) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (t > t_) t_ = t;
    }

private:
    std::mutex mutex_;
    double     t_{1000.0};
};

// ------------------------------------------------------------- le faux reseau ---
struct FakeDevice {
    bool                   ping{true};
    double                 ms{1.5};
    std::string            mac, name;
    std::vector<int>       open;
    bool                   refuses{false};            // un port ferme : "connexion refusee"
    modbus::Identification identity;                  // vide : exception 1 (identification non servie)
    int                    answerUnit{-1};            // -1 : tout esclave ; sinon les autres : exception 11
    bool                   currentUnsupported{false}; // idCode 2 : exception 3 (la base seulement)
    bool                   garbage{false};            // une reponse inattendue (trame illisible)
};

class FakeProber;

class FakeLine final : public ipscan::Prober::Line {
public:
    FakeLine(FakeProber& parent, FakeDevice device) : parent_(parent), device_(std::move(device)) {}
    modbus::Outcome exchange(const modbus::Frame& rq, modbus::Frame& rs) override;

private:
    FakeProber& parent_;
    FakeDevice  device_;
};

class FakeProber final : public ipscan::Prober {
public:
    explicit FakeProber(std::shared_ptr<ipscan::Clock> clock) : clock_(std::move(clock)) {}
    std::map<std::uint32_t, FakeDevice> devices;
    std::atomic<int>                    pingDelayMs{0};   // un vrai delai (Arreter), en millisecondes

    PingResult ping(std::uint32_t at, int) override {
        record();
        if (const int d = pingDelayMs.load(); d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d));
        PingResult r;
        const auto it = devices.find(at);
        if (it != devices.end() && it->second.ping) {
            r.ok = true;
            r.ms = it->second.ms;
        } else {
            r.why = "pas de r\xC3\xA9ponse";
        }
        return r;
    }
    PingResult connect(std::uint32_t at, int port, int) override {
        record();
        PingResult r;
        const auto it = devices.find(at);
        if (it == devices.end()) {
            r.why = "pas de r\xC3\xA9ponse";
            return r;
        }
        if (std::find(it->second.open.begin(), it->second.open.end(), port) != it->second.open.end()) {
            r.ok = true;
            r.ms = 2.0;
        } else {
            r.why = it->second.refuses ? "connexion refus\xC3\xA9" "e" : "pas de r\xC3\xA9ponse";
        }
        return r;
    }
    std::string arp(std::uint32_t at) override {
        const auto it = devices.find(at);
        return it == devices.end() ? std::string{} : it->second.mac;
    }
    std::string name(std::uint32_t at) override {
        const auto it = devices.find(at);
        return it == devices.end() ? std::string{} : it->second.name;
    }
    std::unique_ptr<Line> modbus(std::uint32_t at, int port, int, std::string* why) override {
        record();
        const auto it = devices.find(at);
        if (it == devices.end() || std::find(it->second.open.begin(), it->second.open.end(), port) == it->second.open.end()) {
            if (why) *why = "connexion impossible";
            return nullptr;
        }
        return std::make_unique<FakeLine>(*this, it->second);
    }

    void record() {
        const double t = clock_->now();
        std::lock_guard<std::mutex> lock(mutex_);
        times_.push_back(t);
    }
    void frame(const modbus::Frame& f) {
        std::lock_guard<std::mutex> lock(mutex_);
        frames_.push_back(f);
    }
    std::vector<double> times() {
        std::lock_guard<std::mutex> lock(mutex_);
        return times_;
    }
    std::vector<modbus::Frame> frames() {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_;
    }
    std::size_t calls() {
        std::lock_guard<std::mutex> lock(mutex_);
        return times_.size();
    }

private:
    std::shared_ptr<ipscan::Clock> clock_;
    std::mutex                     mutex_;
    std::vector<double>            times_;
    std::vector<modbus::Frame>     frames_;
};

modbus::Outcome FakeLine::exchange(const modbus::Frame& rq, modbus::Frame& rs) {
    parent_.record();
    parent_.frame(rq);
    modbus::Outcome o;
    if (device_.garbage) {
        rs = {0x00, 0x01, 0x00};
        o.lost = true;
        o.why = "trame illisible";
        return o;
    }
    modbus::Request q;
    int exception = 0;
    if (!modbus::decodeRequest(rq.data(), rq.size(), q, exception)) {
        o.lost = true;
        return o;
    }
    modbus::Response r;
    r.transaction = q.transaction;
    r.unit = q.unit;
    r.function = q.function;
    if (q.function != modbus::kReadDeviceIdentification) r.exception = 1;
    else if (device_.answerUnit >= 0 && q.unit != device_.answerUnit) r.exception = 11;
    else if (q.idCode == 2 && device_.currentUnsupported) r.exception = 3;
    else if (device_.identity.empty()) r.exception = 1;
    else r.objects = device_.identity;
    rs = modbus::encode(r);
    if (r.exception) {
        o.exception = r.exception;
        o.why = "exception";
    } else {
        o.ok = true;
    }
    return o;
}

// Le PC : eth1 en 192.168.1.2 / 24.
std::vector<hmi::netinfo::Adapter> pcAdapters() {
    hmi::netinfo::Adapter a;
    a.name = "eth1";
    a.kind = hmi::netinfo::Adapter::Kind::Ethernet;
    a.up = true;
    a.addresses = {{ip("192.168.1.2"), 24}};
    hmi::netinfo::Adapter big;
    big.name = "usine";
    big.kind = hmi::netinfo::Adapter::Kind::Ethernet;
    big.up = true;
    big.addresses = {{ip("10.20.30.40"), 16}};
    hmi::netinfo::Adapter off;
    off.name = "wifi";
    off.kind = hmi::netinfo::Adapter::Kind::WiFi;
    off.up = false;
    return {a, big, off};
}

// Le reseau simule de 192.168.1.0/24.
void populate(FakeProber& net) {
    FakeDevice plc;                                        // .10 : un M340, il s'identifie (esclave 255)
    plc.mac = "00-80-F4-0C-12-10";
    plc.open = {502, 80};
    plc.identity = {{0, "Schneider Electric"}, {1, "BMXP342020"}, {2, "V3.20"}, {4, "M340"}};
    net.devices[ip("192.168.1.10")] = plc;
    FakeDevice gw;                                         // .11 : seul l'esclave 1 repond (une passerelle)
    gw.mac = "00-90-E8-5A-01-32";
    gw.open = {502};
    gw.answerUnit = 1;
    gw.identity = {{0, "Moxa"}, {1, "MGate 5105"}};
    net.devices[ip("192.168.1.11")] = gw;
    FakeDevice base;                                       // .12 : la base seulement (idCode 1), un nom DNS
    base.mac = "00-80-F4-0C-12-12";
    base.name = "centrale.usine.local";
    base.open = {502};
    base.currentUnsupported = true;
    base.identity = {{0, "Schneider Electric"}, {1, "PM5560"}};
    net.devices[ip("192.168.1.12")] = base;
    FakeDevice odd;                                        // .13 : le 502 ouvert, une reponse inattendue
    odd.open = {502};
    odd.garbage = true;
    net.devices[ip("192.168.1.13")] = odd;
    FakeDevice s7;                                         // .14 : pas de ping, l'ARP, un S7 (102)
    s7.ping = false;
    s7.mac = "00-1B-1B-01-02-03";
    s7.open = {102};
    net.devices[ip("192.168.1.14")] = s7;
    FakeDevice scale;                                      // .15 : une balance qui s'identifie
    scale.mac = "00-11-22-33-44-15";
    scale.open = {502};
    scale.identity = {{0, "Mettler"}, {4, "IND780"}};
    net.devices[ip("192.168.1.15")] = scale;
    FakeDevice ghost;                                      // .16 : bloque le ping, pas d'ARP, refuse : invisible sur le reseau local
    ghost.ping = false;
    ghost.refuses = true;
    net.devices[ip("192.168.1.16")] = ghost;
    FakeDevice web1;                                       // .18 et .19 : la meme MAC (un conflit)
    web1.mac = "00-AA-BB-CC-DD-EE";
    web1.open = {80};
    net.devices[ip("192.168.1.18")] = web1;
    net.devices[ip("192.168.1.19")] = web1;
    FakeDevice self;                                       // .2 : ce PC
    self.mac = "00-1B-21-3A-4F-10";
    net.devices[ip("192.168.1.2")] = self;
}

// Lance un scan sur le faux reseau et attend la fin (sans dormir : le faux temps ne s'attend pas).
std::vector<ipscan::Host> runScan(ipscan::Scanner& sc, const ipscan::Options& o, ipscan::Scanner::Progress* progress = nullptr) {
    std::string why;
    const bool started = sc.start(o, ipscan::grant(o, "essai@banc", "2026-10-31 14:00:00.000"), &why);
    check(started, "le scan simul\xC3\xA9 d\xC3\xA9marre (" + why + ")");
    for (int i = 0; i < 20000 && sc.running(); ++i) std::this_thread::sleep_for(std::chrono::microseconds(500));
    check(!sc.running(), "le scan simul\xC3\xA9 se termine");
    if (progress) *progress = sc.progress();
    return sc.results();
}

// =============================================================================
//  1. Les plages et leurs bornes
// =============================================================================
void plages() {
    std::printf("les plages, le masque, l'interface, les bornes\n");
    std::uint32_t a = 0, b = 0;
    std::string why;
    check(ipscan::parseRange("192.168.1.0/24", a, b) && b - a + 1 == 254, "/24 : 254 adresses");
    check(ipscan::parseRange("10.0.0.0/20", a, b) && b - a + 1 == 4094, "/20 : 4094 adresses (sous la borne)");
    check(!ipscan::parseRange("10.0.0.0/19", a, b, &why) && has(why, "4096"), "/19 : refus\xC3\xA9, 4096 au plus");
    check(ipscan::parseRange("10.0.0.0-10.0.15.255", a, b) && b - a + 1 == 4096, "4096 adresses exactement : accept\xC3\xA9");
    check(!ipscan::parseRange("10.0.0.0-10.0.16.0", a, b, &why), "4097 adresses : refus\xC3\xA9");
    check(!ipscan::parseRange("192.168.1.300", a, b, &why), "une adresse impossible : refus\xC3\xA9" "e");

    const auto adapters = pcAdapters();
    const auto list = disc::interfaces(adapters);
    check(list.size() == 3 && list[0].usable && !list[2].usable && has(list[2].label, "branch"), "les interfaces du PC, la d\xC3\xA9" "branch\xC3\xA9" "e dite");
    check(disc::defaultInterface(adapters) == "eth1", "l'interface propos\xC3\xA9" "e : la premi\xC3\xA8re Ethernet branch\xC3\xA9" "e");

    disc::Request r;
    check(disc::useInterface(r, adapters, "eth1", &why) && r.start == "192.168.1.1" && r.end == "192.168.1.254" && r.mask == "24",
          "eth1 : son r\xC3\xA9seau (d\xC3\xA9" "but, fin, masque)");
    disc::Request big;
    check(disc::useInterface(big, adapters, "usine", &why) && big.start == "10.20.30.1" && big.end == "10.20.30.254" && big.mask == "24",
          "un /16 : le /24 du PC (jamais 65534 adresses d'office)");
    check(!disc::useInterface(big, adapters, "eth9", &why) && has(why, "inconnue"), "une interface inconnue : refus\xC3\xA9" "e");
    check(!disc::useInterface(big, adapters, "wifi", &why), "une interface sans adresse : refus\xC3\xA9" "e");

    disc::Request m = r;
    m.start = "192.168.7.77";
    m.mask = "255.255.255.192";
    check(disc::applyMask(m, &why) && m.start == "192.168.7.65" && m.end == "192.168.7.126", "le masque /26 appliqu\xC3\xA9 au d\xC3\xA9" "but");
    m.mask = "255.0.255.0";
    check(!disc::applyMask(m, &why), "un masque troue : refus\xC3\xA9");
    check(disc::setRange(m, "172.16.0.0/28", &why) && m.start == "172.16.0.1" && m.end == "172.16.0.14" && m.mask == "28", "une plage d'un trait (CIDR)");
    check(disc::setRange(m, "172.16.0.5-9", &why) && m.end == "172.16.0.9" && m.mask.empty(), "une plage d\xC3\xA9" "but-fin : sans masque");

    // Le plan.
    disc::Plan p = disc::plan(r, adapters);
    check(p.ok() && p.addresses == 254 && p.options.onLink && p.options.threads == ipscan::kDefaultThreads && p.options.rate == ipscan::kDefaultRate,
          "le plan de eth1 : 254 adresses, l'ARP, 8 fils, 20 par seconde");
    check(p.options.ports == std::vector<int>({502, 102, 44818, 4840, 80, 443, 9100}), "les protocoles par d\xC3\xA9" "faut : leurs ports");
    check(p.options.modbusId && has(p.interfaceText, "eth1") && has(p.maskText, "255.255.255.0"), "l'identification Modbus, l'interface, le masque");
    disc::Request bad = r;
    bad.end = "192.168.17.1";
    check(!disc::plan(bad, adapters).ok() && has(disc::plan(bad, adapters).error, "4096"), "plus de 4096 adresses : le plan refuse");
    bad = r;
    bad.end = "192.168.0.9";
    check(!disc::plan(bad, adapters).ok(), "une fin avant le d\xC3\xA9" "but : refus\xC3\xA9");
    bad = r;
    bad.start = "224.0.0.1";
    bad.end = "224.0.0.5";
    check(!disc::plan(bad, adapters).ok(), "la multidiffusion : refus\xC3\xA9" "e");
    bad = r;
    bad.protocols = {"modbus", "telnet"};
    check(!disc::plan(bad, adapters).ok() && has(disc::plan(bad, adapters).error, "telnet"), "un protocole inconnu : refus\xC3\xA9");
    bad = r;
    bad.rate = 500;
    check(!disc::plan(bad, adapters).ok() && has(disc::plan(bad, adapters).error, "cadence"), "500 par seconde : refus\xC3\xA9 (100 au plus)");
    bad.rate = 0;
    check(!disc::plan(bad, adapters).ok(), "0 par seconde : refus\xC3\xA9");
    bad = r;
    bad.threads = 64;
    check(!disc::plan(bad, adapters).ok(), "64 fils : refus\xC3\xA9 (32 au plus)");
    disc::Request few = r;
    few.protocols = {"s7"};
    few.extraPorts = "5020";
    const auto pf = disc::plan(few, adapters);
    check(pf.ok() && pf.options.ports == std::vector<int>({102, 5020}) && !pf.options.modbusId, "S7 et un port en plus : sans 502, pas d'identification");
    disc::Request off = r;
    off.adapter.clear();
    off.start = "10.99.0.1";
    off.end = "10.99.0.20";
    const auto po = disc::plan(off, adapters);
    check(po.ok() && !po.options.onLink && has(po.interfaceText, "aucune") && !po.notes.empty(), "une plage hors des r\xC3\xA9seaux du PC : sans ARP, dit");
    disc::Request elsewhere = r;
    elsewhere.start = "10.20.31.1";
    elsewhere.end = "10.20.31.9";
    const auto pe = disc::plan(elsewhere, adapters);
    check(pe.ok() && std::any_of(pe.notes.begin(), pe.notes.end(), [](const std::string& n) { return has(n, "routage"); }),
          "une plage hors du r\xC3\xA9seau de l'interface choisie : la note le dit");

    // L'Assistant IA : une plage d'un trait, des protocoles par leur nom, une cadence.
    disc::Request ai;
    check(disc::requestFrom("192.168.1.0/24", {"Modbus TCP", "siemens", "http"}, 0, ai, adapters, &why) && ai.adapter == "eth1"
              && ai.protocols == std::vector<std::string>({"modbus", "s7", "web"}) && ai.rate == ipscan::kDefaultRate && ai.origin == "Assistant IA"
              && ai.start == "192.168.1.1" && ai.mask == "24",
          "l'IA : la plage, les protocoles par leurs noms, la cadence par d\xC3\xA9" "faut, l'interface du r\xC3\xA9seau");
    check(!disc::requestFrom("192.168.1.0/24", {"telnet"}, 0, ai, adapters, &why) && has(why, "telnet"), "l'IA : un protocole inconnu, refus\xC3\xA9");
    check(!disc::requestFrom("192.168.1.0/24", {}, 500, ai, adapters, &why), "l'IA : 500 interrogations par seconde, refus\xC3\xA9");
    check(!disc::requestFrom("10.0.0.0/8", {}, 0, ai, adapters, &why) && has(why, "4096"), "l'IA : un /8, refus\xC3\xA9");
    check(disc::requestFrom("10.99.0.7", {}, 5, ai, adapters, &why) && ai.adapter.empty() && ai.start == ai.end && ai.rate == 5 && disc::plan(ai, adapters).ok(),
          "l'IA : une seule adresse hors des r\xC3\xA9seaux du PC, 5 par seconde");
    check(disc::protocol("EtherNet/IP") && disc::protocol("EtherNet/IP")->key == "enip" && disc::protocol("OPC UA") && !disc::protocol(""),
          "les protocoles par leur libell\xC3\xA9");

    // Le scanner refuse aussi, accord ou pas.
    ipscan::Scanner sc(std::make_shared<FakeProber>(std::make_shared<FakeClock>()), std::make_shared<FakeClock>());
    ipscan::Options o;
    o.first = ip("10.0.0.0");
    o.last = ip("10.0.16.0");
    check(!sc.start(o, ipscan::grant(o), &why) && has(why, "4096"), "le scanner refuse 4097 adresses, m\xC3\xAAme confirm\xC3\xA9" "es");
    o.last = ip("10.0.0.9");
    o.rate = 1000;
    check(!sc.start(o, ipscan::grant(o), &why) && has(why, "cadence"), "le scanner refuse une cadence de 1000 par seconde");
    o.rate = 20;
    o.threads = 64;
    check(!sc.start(o, ipscan::grant(o), &why), "le scanner refuse 64 fils");
}

// =============================================================================
//  2. La cadence
// =============================================================================
void cadence() {
    std::printf("la cadence : N interrogations par seconde au plus\n");
    const ipscan::Options defaults;
    check(defaults.rate == 20 && defaults.threads == 8, "par d\xC3\xA9" "faut : 20 par seconde, 8 fils");

    auto clock = std::make_shared<FakeClock>();
    ipscan::Pacer pacer(20, clock);
    std::atomic<bool> stop{false};
    const auto real0 = std::chrono::steady_clock::now();
    const double t0 = clock->now();
    std::vector<double> at;
    bool granted = true;
    for (int i = 0; i < 100; ++i) {
        if (!pacer.wait(stop)) granted = false;
        at.push_back(clock->now());
    }
    check(granted, "100 cr\xC3\xA9neaux d'affil\xC3\xA9" "e");
    bool spaced = true;
    for (std::size_t i = 1; i < at.size(); ++i)
        if (at[i] - at[i - 1] < 0.05 - 1e-9) spaced = false;
    check(spaced, "deux interrogations : 50 ms d'\xC3\xA9" "cart au moins (20 par seconde)");
    check(at.front() == t0, "la premi\xC3\xA8re part tout de suite (pas d'attente inutile)");
    check(std::abs((at.back() - t0) - 99 * 0.05) < 1e-6, "100 interrogations : 4,95 s de faux temps");
    // Aucune fenetre d'une seconde n'en compte plus de 20.
    std::size_t worst = 0;
    for (std::size_t i = 0; i < at.size(); ++i) {
        std::size_t n = 0;
        for (std::size_t j = i; j < at.size() && at[j] < at[i] + 1.0 - 1e-9; ++j) ++n;
        worst = std::max(worst, n);
    }
    check(worst <= 20, "jamais plus de 20 dans une seconde (" + std::to_string(worst) + ")");
    check(pacer.granted() == 100, "100 cr\xC3\xA9neaux donn\xC3\xA9s");
    const double real = std::chrono::duration<double>(std::chrono::steady_clock::now() - real0).count();
    check(real < 2.0, "le faux temps : aucune vraie attente");

    // Plusieurs fils, un seul rythme.
    auto clock2 = std::make_shared<FakeClock>();
    ipscan::Pacer shared(10, clock2);
    const double s0 = clock2->now();
    std::vector<std::thread> threads;
    for (int k = 0; k < 4; ++k)
        threads.emplace_back([&] {
            for (int i = 0; i < 25; ++i) (void)shared.wait(stop);
        });
    for (auto& t : threads) t.join();
    check(shared.granted() == 100 && clock2->now() - s0 >= 99 * 0.1 - 1e-6, "4 fils, 10 par seconde : 100 interrogations, 9,9 s au moins");

    // Arreter pendant l'attente : rien ne part.
    stop = true;
    check(!pacer.wait(stop) && pacer.granted() == 100, "Arr\xC3\xAAter : plus de cr\xC3\xA9neau");
    // Avec la vraie horloge : un creneau a 1 seconde, Arreter apres 30 ms - la main revient vite.
    ipscan::Pacer slow(1);
    std::atomic<bool> halt{false};
    (void)slow.wait(halt);
    std::atomic<bool> second{true};
    const auto w0 = std::chrono::steady_clock::now();
    std::thread waiter([&] { second = slow.wait(halt); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    halt = true;
    waiter.join();
    const double waited = std::chrono::duration<double>(std::chrono::steady_clock::now() - w0).count();
    check(!second && waited < 0.5, "Arr\xC3\xAAter pendant l'attente d'un cr\xC3\xA9neau : rendu en moins de 0,5 s");

    // L'estimation.
    ipscan::Options o;
    o.first = ip("192.168.1.1");
    o.last = ip("192.168.1.254");
    const auto e = disc::estimate(o);
    check(e.lowProbes == 254 && e.lowSeconds >= 254 / 20.0 - 1e-6 && e.highSeconds > e.lowSeconds, "254 adresses : 12,7 s au moins, davantage si tout r\xC3\xA9pond");
    ipscan::Options faster = o;
    faster.rate = 100;
    check(disc::estimate(faster).highSeconds < e.highSeconds, "plus de cadence : moins long");
    check(disc::durationText(12.7) == "13 s" && disc::durationText(125) == "2 min 05 s" && disc::durationText(4000) == "1 h 06 min",
          "la dur\xC3\xA9" "e en texte");

    // Un scan d'un seul fil : chaque interrogation a son creneau, mesure au faux reseau.
    auto clock3 = std::make_shared<FakeClock>();
    auto net = std::make_shared<FakeProber>(clock3);
    populate(*net);
    ipscan::Scanner sc(net, clock3);
    ipscan::Options one;
    one.first = ip("192.168.1.1");
    one.last = ip("192.168.1.20");
    one.threads = 1;
    one.rate = 20;
    ipscan::Scanner::Progress pr;
    (void)runScan(sc, one, &pr);
    const auto times = net->times();
    bool ok = !times.empty();
    for (std::size_t i = 1; i < times.size(); ++i)
        if (times[i] - times[i - 1] < 0.05 - 1e-9) ok = false;
    check(ok, "un fil : chaque interrogation du scan \xC3\xA0 50 ms de la pr\xC3\xA9" "c\xC3\xA9" "dente au moins");
    check(pr.probes == times.size(), "le compte des interrogations : celui du r\xC3\xA9seau (" + std::to_string(pr.probes) + ")");
    // Huit fils : le meme plafond.
    auto clock4 = std::make_shared<FakeClock>();
    auto net4 = std::make_shared<FakeProber>(clock4);
    populate(*net4);
    ipscan::Scanner sc4(net4, clock4);
    ipscan::Options eight = one;
    eight.threads = 8;
    ipscan::Scanner::Progress pr4;
    (void)runScan(sc4, eight, &pr4);
    check(pr4.probes == net4->calls() && pr4.seconds >= (static_cast<double>(pr4.probes) - 1) * 0.05 - 1e-6,
          "8 fils : " + std::to_string(pr4.probes) + " interrogations en " + std::to_string(pr4.seconds) + " s de faux temps (20 par seconde au plus)");
}

// =============================================================================
//  3. La confirmation
// =============================================================================
void confirmation() {
    std::printf("la confirmation : rien ne part sans elle\n");
    auto clock = std::make_shared<FakeClock>();
    auto net = std::make_shared<FakeProber>(clock);
    populate(*net);
    ipscan::Scanner sc(net, clock);
    ipscan::Options o;
    o.first = ip("192.168.1.1");
    o.last = ip("192.168.1.20");
    std::string why;
    check(!sc.start(o, ipscan::Permit{}, &why) && has(why, "confirmation") && !sc.running(), "sans accord : refus\xC3\xA9");
    check(net->calls() == 0, "sans accord : aucune interrogation");
    const auto permit = ipscan::grant(o, "essai@banc", "2026-10-31 14:00:00.000");
    check(permit.valid() && !permit.used() && permit.who() == "essai@banc", "l'accord : qui, quand, pas encore servi");
    ipscan::Options other = o;
    other.last = ip("192.168.1.254");
    check(!sc.start(other, permit, &why) && has(why, "chang"), "l'accord d'une autre plage : refus\xC3\xA9");
    ipscan::Options faster = o;
    faster.rate = 50;
    check(!sc.start(faster, permit, &why), "l'accord d'une autre cadence : refus\xC3\xA9");
    ipscan::Options morePorts = o;
    morePorts.ports.push_back(23);
    check(!sc.start(morePorts, permit, &why), "l'accord sans le port 23 : refus\xC3\xA9");
    check(net->calls() == 0 && !permit.used(), "toujours rien d'envoy\xC3\xA9, l'accord intact");
    check(sc.start(o, permit, &why), "l'accord de ces r\xC3\xA9glages : le scan part");
    for (int i = 0; i < 20000 && sc.running(); ++i) std::this_thread::sleep_for(std::chrono::microseconds(500));
    check(permit.used() && sc.permittedBy() == "essai@banc", "l'accord est servi, son auteur gard\xC3\xA9");
    const std::size_t after = net->calls();
    check(after > 0, "le scan a interrog\xC3\xA9 le r\xC3\xA9seau simul\xC3\xA9");
    check(!sc.start(o, permit, &why) && has(why, "servi"), "le m\xC3\xAAme accord une deuxi\xC3\xA8me fois : refus\xC3\xA9");
    check(net->calls() == after, "le deuxi\xC3\xA8me essai n'a rien envoy\xC3\xA9");
    check(!ipscan::signature(o).empty() && ipscan::signature(o) != ipscan::signature(other), "l'empreinte distingue les r\xC3\xA9glages");
}

// =============================================================================
//  4. La lecture seule
// =============================================================================
void lectureSeule() {
    std::printf("la lecture seule : seule l'identification (43 / 14) part\n");
    std::string why;
    for (const std::uint8_t unit : {std::uint8_t{255}, std::uint8_t{1}})
        for (const std::uint8_t code : {std::uint8_t{1}, std::uint8_t{2}}) {
            const auto f = ipscan::identificationRequest(7, unit, code);
            check(f.size() == 11 && f[7] == 0x2B && f[8] == 0x0E && f[9] == code && f[6] == unit && ipscan::readOnlyFrame(f, &why),
                  "la requ\xC3\xAAte d'identification (esclave " + std::to_string(unit) + ", code " + std::to_string(code) + ") : 43 / 14, 11 octets");
        }
    const auto refused = [&](std::uint8_t function, const char* what) {
        modbus::Request rq;
        rq.transaction = 1;
        rq.unit = 1;
        rq.function = function;
        rq.address = 10;
        rq.count = 2;
        rq.registers = {1, 2};
        rq.bits = {true, false};
        const auto f = modbus::encode(rq);
        why.clear();
        check(!ipscan::readOnlyFrame(f, &why) && !why.empty(), std::string(what) + " : refus\xC3\xA9" "e (" + why + ")");
    };
    refused(modbus::kWriteSingleCoil, "fonction 5 (\xC3\xA9" "crire un bit)");
    refused(modbus::kWriteSingleRegister, "fonction 6 (\xC3\xA9" "crire un mot)");
    refused(modbus::kWriteMultipleCoils, "fonction 15 (\xC3\xA9" "crire des bits)");
    refused(modbus::kWriteMultipleRegisters, "fonction 16 (\xC3\xA9" "crire des mots)");
    refused(modbus::kReadHoldingRegisters, "fonction 3 (lire des mots : pas dans la d\xC3\xA9" "couverte)");
    refused(modbus::kReadCoils, "fonction 1");
    // Des trames ecrites a la main : 22 (masquer un mot), 23 (lire-ecrire), 8 (diagnostic, redemarrage), MEI 13.
    const modbus::Frame mask = {0, 1, 0, 0, 0, 8, 1, 22, 0, 10, 0xFF, 0x00, 0x00, 0x01};
    const modbus::Frame rw = {0, 1, 0, 0, 0, 13, 1, 23, 0, 0, 0, 1, 0, 10, 0, 1, 2, 0, 5};
    const modbus::Frame restart = {0, 1, 0, 0, 0, 6, 1, 8, 0, 1, 0, 0};
    const modbus::Frame canopen = {0, 1, 0, 0, 0, 5, 1, 0x2B, 0x0D, 1, 0};
    const modbus::Frame longer = {0, 1, 0, 0, 0, 6, 1, 0x2B, 0x0E, 1, 0, 0};
    const modbus::Frame lying = {0, 1, 0, 0, 0, 9, 1, 0x2B, 0x0E, 1, 0};
    const modbus::Frame notModbus = {0, 1, 0, 1, 0, 5, 1, 0x2B, 0x0E, 1, 0};
    const modbus::Frame badCode = {0, 1, 0, 0, 0, 5, 1, 0x2B, 0x0E, 9, 0};
    check(!ipscan::readOnlyFrame(mask), "fonction 22 (masquer un mot) : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(rw), "fonction 23 (lire et \xC3\xA9" "crire) : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(restart), "fonction 8 (diagnostic, red\xC3\xA9marrage) : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(canopen), "fonction 43, MEI 13 (CANopen) : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(longer), "une identification trop longue : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(lying), "une longueur d'en-t\xC3\xAAte fausse : refus\xC3\xA9" "e");
    check(!ipscan::readOnlyFrame(notModbus), "un protocole non nul : refus\xC3\xA9");
    check(!ipscan::readOnlyFrame(badCode), "un code d'identification 9 : refus\xC3\xA9");
    check(!ipscan::readOnlyFrame({}) && !ipscan::readOnlyFrame({0, 1, 0}), "une trame vide ou tronqu\xC3\xA9" "e : refus\xC3\xA9" "e");

    // Tout ce qui part pendant un scan simule : des lectures d'identification, rien d'autre.
    auto clock = std::make_shared<FakeClock>();
    auto net = std::make_shared<FakeProber>(clock);
    populate(*net);
    ipscan::Scanner sc(net, clock);
    ipscan::Options o;
    o.first = ip("192.168.1.1");
    o.last = ip("192.168.1.20");
    (void)runScan(sc, o);
    const auto frames = net->frames();
    check(frames.size() >= 5, "des trames Modbus sont parties (" + std::to_string(frames.size()) + ")");
    bool allRead = true;
    for (const auto& f : frames) {
        if (!ipscan::readOnlyFrame(f)) allRead = false;
        if (f.size() > 7) {
            const int fc = f[7];
            if (fc == 5 || fc == 6 || fc == 15 || fc == 16 || fc == 22 || fc == 23 || fc == 8) allRead = false;
        }
    }
    check(allRead, "chaque trame partie : une lecture d'identification (aucune \xC3\xA9" "criture, aucun diagnostic)");
}

// =============================================================================
//  5. Un scan simule et la fiche de chaque appareil
// =============================================================================
void scanSimule() {
    std::printf("un scan simul\xC3\xA9 : accessible, inaccessible, incomplet, inattendu, Arr\xC3\xAAter\n");
    auto clock = std::make_shared<FakeClock>();
    auto net = std::make_shared<FakeProber>(clock);
    populate(*net);
    ipscan::Scanner sc(net, clock);
    ipscan::Options o;
    o.first = ip("192.168.1.1");
    o.last = ip("192.168.1.20");
    ipscan::Scanner::Progress pr;
    const auto hosts = runScan(sc, o, &pr);
    check(pr.finished && !pr.stopped && pr.done == 20 && pr.total == 20, "20 adresses essay\xC3\xA9" "es, termin\xC3\xA9");
    const auto find = [&](const char* a) -> const ipscan::Host* {
        for (const auto& h : hosts)
            if (h.address == a) return &h;
        return nullptr;
    };
    const auto* plc = find("192.168.1.10");
    check(plc && plc->ping && plc->hasPort(502) && plc->hasPort(80) && plc->modbusAnswered && plc->modbusUnit == 255 && plc->identity.size() == 4,
          "l'automate .10 : accessible, 502 et 80, identifi\xC3\xA9 (esclave 255)");
    const auto* gw = find("192.168.1.11");
    check(gw && gw->modbusAnswered && gw->modbusUnit == 1 && has(gw->modbus, "MGate"), "la passerelle .11 : l'esclave 1 apr\xC3\xA8s le refus du 255");
    const auto* base = find("192.168.1.12");
    check(base && base->modbusAnswered && has(base->modbus, "PM5560") && base->name == "centrale.usine.local",
          "la centrale .12 : l'identification de base apr\xC3\xA8s le refus de la courante");
    const auto* odd = find("192.168.1.13");
    check(odd && odd->hasPort(502) && !odd->modbusAnswered && odd->modbus.empty() && odd->identity.empty(),
          "une r\xC3\xA9ponse inattendue (.13) : rien d'identifi\xC3\xA9, rien d'invent\xC3\xA9");
    const auto* s7 = find("192.168.1.14");
    check(s7 && !s7->ping && s7->how == "ARP" && s7->hasPort(102), "le S7 .14 : sans ping, vu par la table ARP");
    check(!find("192.168.1.16") && !find("192.168.1.3"), "un appareil muet (.16) et une adresse vide : pas trouv\xC3\xA9s");
    check(std::is_sorted(hosts.begin(), hosts.end(), [](const ipscan::Host& a, const ipscan::Host& b) { return a.ip < b.ip; }), "par adresse croissante");

    // La fiche.
    const auto d10 = disc::classify(*plc);
    check(d10.vendor == "Schneider Electric" && d10.vendorCertainty == Certainty::Announced, ".10 : le constructeur annonc\xC3\xA9 par l'appareil");
    check(d10.model == "M340" && d10.modelCertainty == Certainty::Announced, ".10 : le mod\xC3\xA8le annonc\xC3\xA9 (nom du produit)");
    check(d10.fact("version") && d10.fact("version")->value == "V3.20", ".10 : la version annonc\xC3\xA9" "e");
    check(d10.typeCertainty == Certainty::Supposed && has(d10.type, "probable") && d10.industrial, ".10 : automate probable (suppos\xC3\xA9)");
    bool modbusConfirmed = false, webSupposed = false;
    for (const auto* f : d10.all("protocole")) {
        if (f->value == "Modbus TCP" && f->certainty == Certainty::Confirmed) modbusConfirmed = true;
        if (f->value == "HTTP" && f->certainty == Certainty::Supposed) webSupposed = true;
    }
    check(modbusConfirmed && webSupposed, ".10 : Modbus TCP confirm\xC3\xA9 (une r\xC3\xA9ponse), HTTP suppos\xC3\xA9 (le port seul)");
    check(d10.fact("ip")->certainty == Certainty::Confirmed && d10.fact("acces")->certainty == Certainty::Confirmed
              && d10.fact("temps") && d10.fact("mac")->certainty == Certainty::Confirmed,
          ".10 : l'adresse, l'accessibilit\xC3\xA9, le temps de r\xC3\xA9ponse et la MAC confirm\xC3\xA9s");
    const auto d14 = disc::classify(*s7);
    check(d14.vendor == "Siemens" && d14.vendorCertainty == Certainty::Supposed, ".14 : Siemens suppos\xC3\xA9 (pr\xC3\xA9" "fixe MAC)");
    check(d14.model.empty() && d14.fact("modele")->certainty == Certainty::Unknown && has(d14.fact("modele")->value, "non identifi"),
          ".14 : aucun mod\xC3\xA8le annonc\xC3\xA9 - non identifi\xC3\xA9, jamais devin\xC3\xA9");
    check(d14.fact("temps") && d14.fact("temps")->source == "connexion TCP", ".14 : le temps de r\xC3\xA9ponse d'apr\xC3\xA8s la connexion (pas de ping)");
    const auto d13 = disc::classify(*odd);
    bool modbusSupposed = false;
    for (const auto* f : d13.all("protocole"))
        if (f->value == "Modbus TCP" && f->certainty == Certainty::Supposed) modbusSupposed = true;
    check(modbusSupposed && d13.vendorCertainty == Certainty::Unknown && d13.modelCertainty == Certainty::Unknown,
          ".13 : Modbus suppos\xC3\xA9 (le 502 ouvert), constructeur et mod\xC3\xA8le inconnus");
    // Jamais un modele invente : sans identification, le modele est toujours inconnu.
    bool invented = false;
    for (const auto& h : hosts) {
        const auto d = disc::classify(h);
        if (h.identity.empty() && (!d.model.empty() || d.modelCertainty != Certainty::Unknown)) invented = true;
    }
    check(!invented, "sans identification annonc\xC3\xA9" "e : aucun mod\xC3\xA8le, pour aucun appareil");
    ipscan::Host bare;
    bare.ip = ip("192.168.1.99");
    bare.address = "192.168.1.99";
    bare.how = "TCP (refus)";
    const auto dBare = disc::classify(bare);
    check(dBare.fact("mac")->certainty == Certainty::Unknown && dBare.typeCertainty == Certainty::Unknown && !dBare.fact("temps"),
          "des informations incompl\xC3\xA8tes : inconnues, dites comme telles");
    check(disc::valueWithCertainty("Siemens", Certainty::Supposed) == "Siemens (suppos\xC3\xA9)" && disc::valueWithCertainty("", Certainty::Unknown) == "non identifi\xC3\xA9",
          "la certitude dans le texte");
    const std::string text = disc::deviceText(d10);
    check(has(text, "Constructeur : Schneider Electric (annonc") && has(text, "Type : automate") && has(text, "suppos"), "la fiche en texte");

    // Arreter : un scan long (un vrai delai de 2 ms par ping), arrete en route.
    auto clock2 = std::make_shared<FakeClock>();
    auto slow = std::make_shared<FakeProber>(clock2);
    slow->pingDelayMs = 2;
    ipscan::Scanner sc2(slow, clock2);
    ipscan::Options many;
    many.first = ip("10.1.0.1");
    many.last = ip("10.1.15.250");
    many.rate = 100;
    many.onLink = false;
    many.ports = {502};
    std::string why;
    check(sc2.start(many, ipscan::grant(many), &why), "un scan de 4090 adresses d\xC3\xA9marre");
    for (int i = 0; i < 4000 && sc2.progress().done < 16; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    sc2.stop();
    const auto p2 = sc2.progress();
    const std::size_t calls = slow->calls();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    check(!sc2.running() && p2.stopped && !p2.finished && p2.done < p2.total, "Arr\xC3\xAAter : le scan s'arr\xC3\xAAte avant la fin (" + std::to_string(p2.done) + ")");
    check(slow->calls() == calls, "apr\xC3\xA8s Arr\xC3\xAAter : plus aucune interrogation");
}

// =============================================================================
//  6. La comparaison avec le projet
// =============================================================================
hmi::Equipment equipment(hmi::Project& p, const char* name, hmi::EquipmentType type, const char* host, int port, int unit) {
    hmi::Equipment e;
    e.id = p.allocate();
    e.name = name;
    e.type = type;
    e.host = host;
    e.port = port;
    e.unit = unit;
    return e;
}

void comparaison() {
    std::printf("la comparaison avec le projet : les six cas, les propositions\n");
    using T = hmi::EquipmentType;
    hmi::Project p;
    p.comm.mode = "modbus";
    p.comm.host = "192.168.1.20";                                                          // l'automate du projet : muet
    p.equipments.push_back(equipment(p, "Automate_Ligne1", T::ModbusTcp, "192.168.1.10", 502, 255));   // declare, trouve
    p.equipments.push_back(equipment(p, "Passerelle", T::EthernetTcp, "192.168.1.11", 80, 1));        // trouve, incoherente
    p.equipments.push_back(equipment(p, "Variateur_ATV", T::ModbusTcp, "192.168.1.16", 502, 1));      // injoignable
    p.equipments.push_back(equipment(p, "Centrale", T::ModbusTcp, "192.168.1.17", 502, 1));           // a une autre adresse (.12)
    p.equipments.push_back(equipment(p, "Pupitre", T::EthernetTcp, "192.168.1.30", 80, 1));           // hors de la plage
    p.equipments.push_back(equipment(p, "Balance_A", T::ModbusTcp, "192.168.1.40", 502, 1));          // doublon...
    p.equipments.push_back(equipment(p, "Balance_B", T::ModbusTcp, "192.168.1.40", 502, 1));          // ... du projet
    p.equipments.push_back(equipment(p, "Esclave_3", T::ModbusTcp, "192.168.1.40", 502, 3));          // une passerelle : pas un doublon
    auto sim = equipment(p, "Pesage", T::ModbusTcp, "127.0.0.1", 502, 1);                               // seulement simule : a associer
    sim.simulated = true;
    sim.description = "balance Mettler \xC2\xB7 IND780 \xC3\xA0 brancher";
    p.equipments.push_back(sim);
    const hmi::Project before = p;

    auto clock = std::make_shared<FakeClock>();
    auto net = std::make_shared<FakeProber>(clock);
    populate(*net);
    ipscan::Scanner sc(net, clock);
    ipscan::Options o;
    o.first = ip("192.168.1.1");
    o.last = ip("192.168.1.20");
    const auto hosts = runScan(sc, o);

    disc::Scope scope;
    scope.first = o.first;
    scope.last = o.last;
    scope.ports = o.ports;
    scope.local = {ip("192.168.1.2")};
    const auto r = disc::analyze(p, hosts, scope);
    check(p == before, "la comparaison ne touche pas au projet");
    const auto count = [&](disc::Status s, const std::string& who) {
        return std::count_if(r.findings.begin(), r.findings.end(), [&](const disc::Finding& f) { return f.status == s && (who.empty() || f.equipment == who); });
    };
    // 1. declares et trouves.
    check(count(disc::Status::Known, "Automate_Ligne1") == 1 && count(disc::Status::Known, "Passerelle") == 1 && r.known == 2,
          "d\xC3\xA9" "clar\xC3\xA9s et trouv\xC3\xA9s : Automate_Ligne1, Passerelle");
    // 2. absents du projet.
    check(r.fresh == 6 && count(disc::Status::New, "") == 6, "absents du projet : .12, .13, .14, .15, .18, .19 (" + std::to_string(r.fresh) + ")");
    // 3. declares mais injoignables (l'automate du projet compris).
    check(count(disc::Status::Unreachable, "Variateur_ATV") == 1 && count(disc::Status::Unreachable, "automate du projet") == 1
              && count(disc::Status::Unreachable, "Centrale") == 1 && r.unreachable == 3,
          "d\xC3\xA9" "clar\xC3\xA9s mais injoignables : Variateur_ATV, Centrale, l'automate du projet");
    check(count(disc::Status::NotScanned, "Pupitre") == 1 && count(disc::Status::Unreachable, "Pupitre") == 0, "hors de la plage : non conclu");
    // 4. une autre adresse (supposee).
    const auto moved = std::find_if(r.findings.begin(), r.findings.end(), [](const disc::Finding& f) { return f.status == disc::Status::Moved; });
    check(moved != r.findings.end() && moved->equipment == "Centrale" && moved->address == "192.168.1.12" && moved->certainty == Certainty::Supposed && r.moved == 1,
          "Centrale : sans doute \xC3\xA0 192.168.1.12 (le m\xC3\xAAme nom DNS) - suppos\xC3\xA9");
    // 5. les doublons : du projet, et du reseau (une MAC a deux adresses).
    check(count(disc::Status::Duplicate, "Balance_A") == 1 && count(disc::Status::Duplicate, "Esclave_3") == 0, "doublon du projet : Balance_A et Balance_B (pas l'esclave 3)");
    check(std::any_of(r.findings.begin(), r.findings.end(), [](const disc::Finding& f) { return f.status == disc::Status::Duplicate && f.address == "192.168.1.18" && has(f.text, "MAC"); })
              && r.duplicates == 2,
          "doublon du r\xC3\xA9seau : la m\xC3\xAAme MAC \xC3\xA0 .18 et .19");
    // 6. les incoherences.
    check(count(disc::Status::Inconsistent, "Passerelle") == 2 && r.inconsistent == 2, "Passerelle : le port 80 ferm\xC3\xA9, et elle r\xC3\xA9pond en Modbus");
    check(count(disc::Status::ThisPc, "") == 1 && r.local == 1 && r.projectText("192.168.1.2") == "ce PC", "ce PC : ni nouveau ni propos\xC3\xA9");

    // Les propositions.
    const auto* update = r.proposalFor("192.168.1.12");
    check(update && update->kind == disc::Proposal::Kind::UpdateAddress && update->equipment == "Centrale" && update->prop("hote") == "192.168.1.12"
              && update->before == "192.168.1.17" && update->certainty == Certainty::Supposed && has(update->reason, "suppos"),
          "mettre \xC3\xA0 jour l'adresse de Centrale (une supposition, dite)");
    const auto* assoc = r.proposalFor("192.168.1.15");
    check(assoc && assoc->kind == disc::Proposal::Kind::Associate && assoc->equipment == "Pesage", "associer .15 \xC3\xA0 l'esclave simul\xC3\xA9 Pesage (son identification)");
    const auto* add14 = r.proposalFor("192.168.1.14");
    check(add14 && add14->kind == disc::Proposal::Kind::Add && add14->prop("type") == "ethernet_tcp" && add14->prop("port") == "102"
              && add14->prop("adresse") == "192.168.1.14" && !add14->equipment.empty() && !p.equipmentByName(add14->equipment),
          "ajouter .14 : Ethernet TCP/IP, le port 102, un nom libre");
    const auto* add13 = r.proposalFor("192.168.1.13");
    check(add13 && add13->prop("type") == "modbus_tcp" && add13->prop("esclave").empty() && add13->certainty == Certainty::Supposed,
          "ajouter .13 : Modbus TCP suppos\xC3\xA9, l'esclave non devin\xC3\xA9");
    check(!r.proposalFor("192.168.1.10") && !r.proposalFor("192.168.1.11") && !r.proposalFor("192.168.1.2"), "rien \xC3\xA0 proposer pour les d\xC3\xA9" "clar\xC3\xA9s ni pour ce PC");
    const auto* add18 = r.proposalFor("192.168.1.18");
    const auto* add19 = r.proposalFor("192.168.1.19");
    check(add18 && add19 && add18->equipment != add19->equipment, "deux propositions : deux noms diff\xC3\xA9rents");
    bool noVariables = true;
    for (const auto& prop : r.proposals)
        for (const auto& [k, v] : prop.props)
            if (k == "variable" || k == "variables") noVariables = false;
    check(noVariables, "aucune variable invent\xC3\xA9" "e dans les propositions");
    check(r.projectText("192.168.1.10") == "Automate_Ligne1" && has(r.projectText("192.168.1.12"), "Centrale ?")
              && has(r.projectText("192.168.1.11"), "incoh") && r.projectText("192.168.1.14") == "absent du projet",
          "la colonne Dans le projet");
    const std::string text = disc::reportText(r);
    check(has(text, "Propositions") && has(text, "Centrale") && has(text, "non identifi") && has(r.countsText(), "injoignable"), "le compte rendu en texte");

    // Un scan arrete : un equipement non trouve dans la plage n'est pas declare injoignable.
    disc::Scope partial = scope;
    partial.complete = false;
    const auto rp = disc::analyze(p, hosts, partial);
    check(rp.unreachable == 0 && std::count_if(rp.findings.begin(), rp.findings.end(), [](const disc::Finding& f) { return f.status == disc::Status::NotScanned && f.equipment == "Variateur_ATV"; }) == 1,
          "un scan arr\xC3\xAAt\xC3\xA9 : injoignable non conclu");
    // Sans l'automate du projet (une IHM sans API).
    disc::Scope noPlc = scope;
    noPlc.withPlc = false;
    check(disc::analyze(p, hosts, noPlc).unreachable == 2, "sans l'automate du projet : deux injoignables");
}

// =============================================================================
//  7. Le journal, la demande en texte, le resume
// =============================================================================
void journal() {
    std::printf("le journal des d\xC3\xA9" "couvertes, la demande en texte, le r\xC3\xA9sum\xC3\xA9\n");
    const auto adapters = pcAdapters();
    disc::Request r;
    std::string why;
    (void)disc::useInterface(r, adapters, "eth1", &why);
    r.origin = "Assistant IA";
    const auto plan = disc::plan(r, adapters);
    // La demande en texte : aller et retour.
    disc::Request back;
    check(disc::decodeRequest(disc::encodeRequest(r), back, &why) && back == r, "la demande en texte, relue \xC3\xA0 l'identique");
    check(!disc::decodeRequest("", back, &why), "une demande vide : refus\xC3\xA9" "e");
    // Le resume de la confirmation.
    const auto lines = disc::summary(plan);
    const auto label = [&](const std::string& k) {
        return std::any_of(lines.begin(), lines.end(), [&](const auto& kv) { return kv.first == k && !kv.second.empty(); });
    };
    check(label("Interface") && label("Plage") && label("Masque") && label("Protocoles") && label("Ports TCP") && label("Adresses")
              && label("Dur\xC3\xA9" "e estim\xC3\xA9" "e") && label("Cadence"),
          "le r\xC3\xA9sum\xC3\xA9 : interface, plage, masque, protocoles, ports, adresses, dur\xC3\xA9" "e, cadence");
    const std::string sum = disc::summaryText(plan);
    check(has(sum, "254") && has(sum, "20 interrogations par seconde") && has(sum, "aucune \xC3\xA9" "criture"), "le r\xC3\xA9sum\xC3\xA9 dit la cadence et l'absence d'\xC3\xA9" "criture");

    // Le journal dans un dossier jetable.
    const auto dir = std::filesystem::temp_directory_path() / ("xpg_decouverte_" + disc::newScanId());
    std::filesystem::create_directories(dir);
    const std::string folder = dir.string();
    const auto permit = ipscan::grant(plan.options, "jdupont@PC-ATELIER", "2026-10-31 14:05:12.350");
    const std::string id = disc::newScanId();
    check(id.size() >= 15 && id != disc::newScanId(), "un identifiant de scan neuf \xC3\xA0 chaque fois");
    auto started = disc::startEntry(plan, permit, id, r.origin);
    started.note = "essai ; avec \"guillemets\"";
    check(started.state == "lance" && started.who == "jdupont@PC-ATELIER" && started.addresses == 254 && has(started.protocols, "Modbus TCP"),
          "le lancement : qui, la plage, les protocoles");
    check(disc::appendJournal(started, folder).has_value(), "le lancement \xC3\xA9" "crit tout de suite");
    ipscan::Scanner::Progress pr;
    pr.done = 254;
    pr.total = 254;
    pr.found = 7;
    pr.probes = 300;
    pr.seconds = 15.5;
    pr.finished = true;
    const auto ended = disc::endEntry(started, pr);
    check(ended.state == "termine" && ended.found == 7 && ended.id == id, "la fin : le m\xC3\xAAme identifiant, le nombre d'appareils");
    check(disc::appendJournal(ended, folder).has_value(), "la fin \xC3\xA9" "crite");
    const auto read = disc::readJournal(folder, &why);
    check(read.size() == 2 && read[0] == started && read[1].state == "termine" && read[1].found == 7 && read[1].probes == 300,
          "le journal relu du fichier : deux lignes, \xC3\xA0 l'identique");
    std::ifstream in(disc::journalPath(folder), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string raw = ss.str();
    check(raw.rfind("id;horodatage;qui", 0) == 0 && raw.find("id;horodatage", 1) == std::string::npos, "l'en-t\xC3\xAAte une seule fois");
    check(has(disc::journalPath(folder), "decouvertes.csv") && has(disc::journalPath(folder), "reseau"), "dans le projet : ihm/reseau/decouvertes.csv");
    check(has(disc::journalText(read[1]), "7 appareils") && has(disc::journalText(read[1]), "jdupont"), "la ligne lisible");
    check(disc::appendJournal(started, std::string{}).has_value() && disc::readJournal(std::string{}).empty(), "un projet pas enregistr\xC3\xA9 : rien d'\xC3\xA9" "crit, sans erreur");
    std::vector<disc::JournalEntry> parsed;
    check(disc::parseJournal("id;horodatage\nune ligne cass\xC3\xA9" "e\n", parsed, &why) && parsed.empty() && has(why, "illisible"), "une ligne cass\xC3\xA9" "e : ignor\xC3\xA9" "e, dite");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    check(!disc::currentUser().empty(), "l'utilisateur de la s\xC3\xA9" "ance");
}

} // namespace

int main() {
    plages();
    cadence();
    confirmation();
    lectureSeule();
    scanSimule();
    comparaison();
    journal();
    std::printf("hmi_discovery_test : %d v\xC3\xA9rifications, %d \xC3\xA9" "checs\n", g_ok + g_fail, g_fail);
    return g_fail == 0 ? 0 : 1;
}
