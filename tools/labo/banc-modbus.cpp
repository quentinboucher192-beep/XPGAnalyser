// banc-modbus.cpp - le banc d'essai du reseau de laboratoire (lot 15).
//
// Des equipements Modbus TCP qui vivent (des valeurs qui bougent), et des
// appareils IP muets (un port TCP qui accepte, une camera, une imprimante),
// chacun a son adresse. Lance dans les espaces de noms reseau du laboratoire
// (voir reseau-labo.sh) :
//
//   banc-modbus --modbus 192.168.1.30:502:centrale --tcp 192.168.1.40:9100
//
// Profils : automate, centrale, analyseur, vide. Les flottants (REAL) sont
// ranges poids faible d'abord (a la Schneider). Arret : Ctrl+C / kill.
//
// Lot 17 : la centrale et l'analyseur ont des ZONES comme un vrai appareil
// (au-dela : "adresse illegale", exception 02 ; une table absente : "fonction
// non prise en charge", 01) - la detection des zones et la carte memoire les
// trouvent. L'automate et la passerelle repondent partout.
#include "hmi/HmiModbus.hpp"
#include "hmi/HmiNet.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop = true; }

// Une memoire a zones : table par table, des plages (vide : la table n'existe pas).
struct Range {
    std::uint32_t first, last;
};
class ZonedBank final : public hmi::modbus::MemoryBank {
public:
    std::vector<Range> coils, discrete, input, holding;
    bool zoned{false};
    int readBits(bool isDiscrete, std::uint16_t address, std::uint16_t count, std::vector<bool>& out) override {
        if (const int e = check(isDiscrete ? discrete : coils, address, count)) return e;
        return MemoryBank::readBits(isDiscrete, address, count, out);
    }
    int readRegisters(bool isInput, std::uint16_t address, std::uint16_t count, std::vector<std::uint16_t>& out) override {
        if (const int e = check(isInput ? input : holding, address, count)) return e;
        return MemoryBank::readRegisters(isInput, address, count, out);
    }
    int writeBits(std::uint16_t address, const std::vector<bool>& values) override {
        if (const int e = check(coils, address, static_cast<std::uint16_t>(values.size()))) return e;
        return MemoryBank::writeBits(address, values);
    }
    int writeRegisters(std::uint16_t address, const std::vector<std::uint16_t>& values) override {
        if (const int e = check(holding, address, static_cast<std::uint16_t>(values.size()))) return e;
        return MemoryBank::writeRegisters(address, values);
    }
private:
    int check(const std::vector<Range>& zone, std::uint32_t first, std::uint32_t count) const {
        if (!zoned) return 0;
        if (zone.empty()) return 1;                              // la table n'existe pas
        const std::uint32_t last = first + (count ? count - 1 : 0);
        for (const auto& r : zone)
            if (first >= r.first && last <= r.last) return 0;
        return 2;                                                // adresse illegale
    }
};

struct Device {
    std::string                                    profile;
    std::shared_ptr<ZonedBank>                     bank;
    std::unique_ptr<hmi::modbus::Server>           server;
};

void setReal(hmi::modbus::MemoryBank& b, std::size_t at, float v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    b.setWord(at, static_cast<std::uint16_t>(u & 0xFFFF));        // poids faible d'abord
    b.setWord(at + 1, static_cast<std::uint16_t>(u >> 16));
}

void animate(Device& d, double t) {
    auto& b = *d.bank;
    const double s = std::sin(t / 7.0), c = std::cos(t / 11.0);
    if (d.profile == "centrale") {
        setReal(b, 3000, static_cast<float>(231.0 + 2.5 * s + 0.4 * std::sin(t * 1.7)));   // tension L1 (V), 43001
        setReal(b, 3004, static_cast<float>(400.2 + 3.1 * c));                               // tension composee (V)
        b.setWord(3002, static_cast<std::uint16_t>(std::lround(125 + 18 * s + 6 * std::sin(t / 2.3))));   // courant x10 (A), %MW3002
        setReal(b, 3010, static_cast<float>(48.5 + 7.5 * s));                                // puissance (kW)
        b.setWord(3020, static_cast<std::uint16_t>(std::lround(5000 + 3 * std::sin(t / 3.0))));          // frequence x100
        b.setWord(3030, static_cast<std::uint16_t>(static_cast<long>(t / 3.0) & 0xFFFF));                // energie (kWh)
        b.setWord(100, static_cast<std::uint16_t>(std::lround(13824 + 9000 * s)));                        // une mesure brute 0..27648
    } else if (d.profile == "analyseur") {
        b.setWord(0, static_cast<std::uint16_t>(std::lround(209 + 3 * s)));                  // O2 (pour mille)
        b.setWord(1, static_cast<std::uint16_t>(std::lround(std::max(0.0, 12 + 10 * c))));  // CO (ppm)
        setReal(b, 100, static_cast<float>(38.5 + 1.5 * s));                                 // temperature (degres)
        b.setBit(0, std::fmod(t, 20.0) < 10.0);                                              // en mesure
    } else if (d.profile == "automate") {
        b.setWord(0, static_cast<std::uint16_t>(static_cast<long>(t * 2) & 0xFFFF));         // compteur
        b.setWord(1, static_cast<std::uint16_t>(std::lround(500 + 400 * s)));
        b.setWord(2, static_cast<std::uint16_t>(std::lround(250 + 200 * c)));
        setReal(b, 10, static_cast<float>(12.5 + 4 * s));
        b.setBit(0, std::fmod(t, 4.0) < 2.0);
        b.setBit(1, std::fmod(t, 9.0) < 3.0);
    }
}

hmi::modbus::Identification identity(const std::string& profile) {
    if (profile == "centrale") return {{0, "Banc XpgAnalyzer"}, {1, "Centrale de mesure (banc)"}, {2, "V1.0"}, {4, "Centrale PM (banc)"}};
    if (profile == "analyseur") return {{0, "Banc XpgAnalyzer"}, {1, "Analyseur de gaz (banc)"}, {2, "V2.3"}};
    if (profile == "automate") return {{0, "Banc XpgAnalyzer"}, {1, "Automate (banc)"}, {2, "V3.1"}};
    if (profile == "passerelle") return {{0, "Banc XpgAnalyzer"}, {1, "Passerelle Modbus TCP / RTU (banc)"}, {2, "V1.4"}};
    return {};
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::vector<Device> devices;
    std::vector<std::unique_ptr<hmi::net::Socket>> listeners;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string opt = argv[i], val = argv[i + 1];
        const auto c1 = val.find(':');
        const auto c2 = val.find(':', c1 + 1);
        const std::string host = val.substr(0, c1);
        const int port = std::atoi(val.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1).c_str());
        std::string why;
        if (opt == "--modbus") {
            Device d;
            d.profile = c2 == std::string::npos ? "vide" : val.substr(c2 + 1);
            d.bank = std::make_shared<ZonedBank>();
            d.bank->setIdentification(identity(d.profile));
            if (d.profile == "centrale") {                       // 40001-40200, 43001-43100 ; 30001-30100 ; 00001-00100 ; pas d'entrees TOR
                d.bank->zoned = true;
                d.bank->holding = {{0, 199}, {3000, 3099}};
                d.bank->input = {{0, 99}};
                d.bank->coils = {{0, 99}};
            } else if (d.profile == "analyseur") {               // 40001-40200 ; 30001-30050 ; 00001-00016 ; 10001-10016
                d.bank->zoned = true;
                d.bank->holding = {{0, 199}};
                d.bank->input = {{0, 49}};
                d.bank->coils = {{0, 15}};
                d.bank->discrete = {{0, 15}};
            }
            animate(d, 0);
            d.server = std::make_unique<hmi::modbus::Server>();
            if (!d.server->start(host, port, d.bank, &why)) {
                std::fprintf(stderr, "banc : %s:%d : %s\n", host.c_str(), port, why.c_str());
                return 1;
            }
            std::printf("banc : Modbus %s:%d (%s)\n", host.c_str(), port, d.profile.c_str());
            devices.push_back(std::move(d));
        } else if (opt == "--tcp") {
            auto s = std::make_unique<hmi::net::Socket>();
            if (!s->listen(host, port, &why)) {
                std::fprintf(stderr, "banc : tcp %s:%d : %s\n", host.c_str(), port, why.c_str());
                return 1;
            }
            std::printf("banc : TCP %s:%d\n", host.c_str(), port);
            listeners.push_back(std::move(s));
        }
    }
    std::fflush(stdout);
    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop) {
        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (auto& d : devices) animate(d, t);
        // Les ports muets : accepter, puis refermer (une camera, une imprimante).
        for (auto& l : listeners) {
            auto c = l->accept(10);
            if (c.valid()) c.close();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    for (auto& d : devices) d.server->stop();
    return 0;
}
