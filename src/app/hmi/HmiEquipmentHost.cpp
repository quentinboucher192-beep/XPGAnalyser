// app/hmi/HmiEquipmentHost.cpp - les equipements du reseau : liaisons, simules,
// pings, ports du PC (lot 15).
#include "HmiEquipmentHost.hpp"

#include "../../hmi/HmiEquipment.hpp"
#include "../../hmi/HmiStore.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

double wallNow() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

std::string settingsKey(const hmi::comm::Settings& s) {
    return s.host + ":" + std::to_string(s.port) + "/" + std::to_string(s.unit) + "/" + std::to_string(s.timeoutMs) + "/"
           + std::to_string(s.periodMs) + "/" + std::to_string(s.retryS) + "/" + (s.lowWordFirst ? "f" : "F") + "/"
           + std::to_string(s.maxWords) + "/" + std::to_string(s.maxBits) + "/" + std::to_string(s.gap) + "/" + (s.writes ? "w" : "r") + "/"
           + std::to_string(s.badAfterS);
}


std::string portText(int port) { return std::to_string(port); }

// "14:02:31" : une heure murale, en heure locale.
std::string clockText(double wall) {
    if (wall <= 0) return {};
    const std::time_t t = static_cast<std::time_t>(wall);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return b;
}

} // namespace

EquipmentHost::EquipmentHost() {
    worker_ = std::thread([this] { loop(); });
}

EquipmentHost::~EquipmentHost() { shutdown(); }

void EquipmentHost::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (applyThread_.joinable()) applyThread_.join();
    for (auto& [id, r] : running_) {
        if (r.link) r.link->stop();
        if (r.server) r.server->stop();
    }
    running_.clear();
    statuses_.clear();
}

void EquipmentHost::event(std::string text) {
    events_.push_back(std::move(text));
    if (events_.size() > 200) events_.erase(events_.begin(), events_.begin() + 100);
}

std::vector<std::string> EquipmentHost::takeEvents() {
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

void EquipmentHost::schedule(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        // Un ping deja demande pour cet equipement : inutile d'en ajouter un.
        if (job.kind == Job::Kind::Ping || job.kind == Job::Kind::Modbus)
            for (const auto& j : jobs_)
                if (j.kind == job.kind && j.key == job.key) return;
        if (job.kind == Job::Kind::Adapters)
            for (const auto& j : jobs_)
                if (j.kind == Job::Kind::Adapters) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_all();
}

void EquipmentHost::loop() {
    {
        const std::string name = hmi::netinfo::computerName();
        const std::string system = hmi::netinfo::systemName();
        std::lock_guard<std::mutex> lock(mutex_);
        computer_ = name;
        system_ = system;
    }
    while (true) {
        std::vector<Job> pings;
        std::optional<Job> other;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_.load() || !jobs_.empty(); });
            if (stop_) break;
            while (!jobs_.empty()) {
                Job j = std::move(jobs_.front());
                jobs_.pop_front();
                if (j.kind == Job::Kind::Ping || j.kind == Job::Kind::Modbus) {     // 1.9 : l'essai Modbus du vrai aussi
                    pings.push_back(std::move(j));
                    continue;
                }
                other = std::move(j);
                break;
            }
            busy_ = static_cast<int>(pings.size());
        }
        if (!pings.empty()) runPings(std::move(pings));
        if (other) {
            switch (other->kind) {
                case Job::Kind::Adapters: {
                    auto list = hmi::netinfo::adapters();
                    std::lock_guard<std::mutex> lock(mutex_);
                    adapters_ = std::move(list);
                    adaptersKnown_ = true;
                    adaptersAt_ = wallNow();
                    break;
                }
                case Job::Kind::Address: {
                    std::string mac;
                    const bool used = hmi::netinfo::addressInUse(other->ip, 1000, &mac);
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (addressCheck_.ip == other->ip) {
                        addressCheck_.done = true;
                        addressCheck_.inUse = used;
                        addressCheck_.mac = mac;
                    }
                    break;
                }
                case Job::Kind::Free: {
                    Probe p;
                    p.host = other->host;
                    const auto r = hmi::netinfo::ping(other->host, other->timeoutMs);
                    p.ok = r.ok;
                    p.ms = r.ok ? r.ms : -1;
                    p.why = r.why;
                    if (other->port > 0) {
                        p.portTried = true;
                        const auto t = hmi::netinfo::probeTcp(other->host, other->port, other->timeoutMs);
                        p.portOk = t.ok;
                        p.portMs = t.ok ? t.ms : -1;
                        p.portWhy = t.why;
                    }
                    p.done = true;
                    p.at = wallNow();
                    std::lock_guard<std::mutex> lock(mutex_);
                    manualPing_ = p;
                    break;
                }
                case Job::Kind::Ping: break;
                case Job::Kind::Modbus: break;
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = 0;
    }
}

void EquipmentHost::runPings(std::vector<Job> jobs) {
    // 1.9 : les essais Modbus du vrai appareil (la bascule automatique) : une
    // connexion, une lecture d'une case ; une exception compte (il repond).
    std::vector<std::future<std::pair<std::string, ModbusProbe>>> modbus;
    for (const auto& j : jobs) {
        if (j.kind != Job::Kind::Modbus) continue;
        modbus.push_back(std::async(std::launch::async, [j] {
            ModbusProbe p;
            hmi::modbus::Client::Settings cs;
            cs.host = j.host;
            cs.port = j.port;
            cs.unit = j.unit;
            cs.timeoutMs = j.timeoutMs;
            hmi::modbus::Client c(cs);
            auto o = c.connect();
            if (o.ok) {
                const auto off = static_cast<std::uint16_t>(std::clamp(j.offset, 0, 65535));
                std::vector<bool> bits;
                std::vector<std::uint16_t> regs;
                if (j.area == 0) o = c.readBits(false, off, 1, bits);
                else if (j.area == 1) o = c.readBits(true, off, 1, bits);
                else if (j.area == 3) o = c.readRegisters(true, off, 1, regs);
                else o = c.readRegisters(false, off, 1, regs);
            }
            c.disconnect();
            p.ok = o.ok || o.exception != 0;
            p.why = p.ok ? std::string{} : (o.why.empty() ? std::string("pas de r\xC3\xA9ponse") : o.why);
            p.done = true;
            p.at = wallNow();
            return std::make_pair(j.key, p);
        }));
    }
    std::erase_if(jobs, [](const Job& j) { return j.kind == Job::Kind::Modbus; });
    // Par paquets de 16, en parallele : dix equipements injoignables ne prennent
    // pas dix secondes.
    for (std::size_t first = 0; first < jobs.size(); first += 16) {
        std::vector<std::future<std::pair<std::string, Probe>>> futures;
        for (std::size_t i = first; i < jobs.size() && i < first + 16; ++i) {
            const Job j = jobs[i];
            futures.push_back(std::async(std::launch::async, [j] {
                Probe p;
                p.host = j.host;
                const auto r = hmi::netinfo::ping(j.host, j.timeoutMs);
                p.ok = r.ok;
                p.ms = r.ok ? r.ms : -1;
                p.why = r.why;
                if (j.tryPort && j.port > 0) {
                    p.portTried = true;
                    const auto t = hmi::netinfo::probeTcp(j.host, j.port, j.timeoutMs);
                    p.portOk = t.ok;
                    p.portMs = t.ok ? t.ms : -1;
                    p.portWhy = t.why;
                }
                p.done = true;
                p.at = wallNow();
                return std::make_pair(j.key, p);
            }));
        }
        for (auto& f : futures) {
            auto [key, p] = f.get();
            std::lock_guard<std::mutex> lock(mutex_);
            probes_[key] = std::move(p);
        }
    }
    for (auto& f : modbus) {
        auto [key, p] = f.get();
        std::lock_guard<std::mutex> lock(mutex_);
        modbusProbes_[key] = std::move(p);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!jobs.empty()) testedAt_ = wallNow();
}

void EquipmentHost::primeMemory(Running& r, const hmi::Project& p) {
    if (!r.bank) return;
    // Lot 17 : les valeurs initiales vont dans le jumeau ; une variable nouvelle
    // (ou changee) y met la sienne, les autres cases gardent ce qu'on y a ecrit.
    for (const auto* v : hmi::equip::boundVariables(p, r.equipment)) {
        const std::string key = upperOf(v->name);
        std::string sig = v->address + "|" + v->type + "|" + v->initial + "|" + std::to_string(v->rawMax) + "|" + std::to_string(v->engMax);
        for (const auto& pl : v->places) sig += "|" + pl.path + "=" + pl.address;
        const auto it = r.primed.find(key);
        if (it != r.primed.end() && it->second == sig) continue;
        r.primed[key] = sig;
        hmi::twin::primeVariable(*r.bank, p, r.equipment, *v);
    }
}

bool EquipmentHost::usesTwin(const hmi::Equipment& e) const noexcept {
    return e.modbus() ? e.hasTwin() && (e.simulated || (e.useTwin && !station_)) : e.hasTwin() && (e.simulated || (e.useTwin && !station_));
}

void EquipmentHost::runTwin(Running& r, const hmi::Equipment& e, const hmi::Project& p) {
    const bool wantServer = e.enabled && e.modbus() && e.hasTwin() && e.twinRunning;
    if (r.restart && r.server) {
        if (r.exposed) r.exposed->stop();
        r.exposed.reset();
        r.server->stop();
        r.server.reset();
        r.bank.reset();
        event("Esclave simul\xC3\xA9 " + e.twinLabel() + " : sa m\xC3\xA9moire repart de son d\xC3\xA9part");
    }
    r.restart = false;
    if (wantServer && !r.server) {
        r.bank = std::make_shared<hmi::twin::TwinBank>();
        r.forcedSet = false;
        r.bank->setIdentification({{0, "XpgAnalyzer"}, {1, "esclave virtuel"}, {2, "lot 17"}, {4, e.twinLabel()}});
        r.bank->setZones(e.zones);
        r.primed.clear();
        r.behaviors.reset();
        if (e.twinStart == hmi::TwinStart::Saved) r.bank->load(e.twinMemory);
        r.server = std::make_unique<hmi::modbus::Server>();
        std::string why;
        if (r.server->start("127.0.0.1", 0, r.bank, &why)) {
            event("Esclave simul\xC3\xA9 " + e.twinLabel() + " en marche (li\xC3\xA9 \xC3\xA0 " + e.name + ", 127.0.0.1:" + std::to_string(r.server->port()) + ")");
        } else {
            event("Esclave simul\xC3\xA9 " + e.twinLabel() + " : serveur impossible (" + why + ")");
            r.server.reset();
            r.bank.reset();
        }
    } else if (!wantServer && r.server) {
        if (r.exposed) r.exposed->stop();
        r.exposed.reset();
        r.server->stop();
        r.server.reset();
        r.bank.reset();
        r.primed.clear();
        event("Esclave simul\xC3\xA9 " + e.twinLabel() + " arr\xC3\xAAt\xC3\xA9");
    }
    if (!r.server) return;
    r.bank->setZones(e.zones);
    r.bank->setForcedException(e.twinException);
    // Lot 18 : les cases forcees (posees quand elles changent ; une memoire neuve les recoit).
    if (auto cells = hmi::twin::forcedCells(e.forcings, e.wordOrder != "fort"); !r.forcedSet || cells != r.forced) {
        r.bank->setForced(cells);
        r.forced = std::move(cells);
        r.forcedSet = true;
    }
    r.server->setMute(!e.twinResponds);
    r.server->setDelay(e.twinDelayMs, e.twinJitterMs);
    if (e.twinStart == hmi::TwinStart::Initial) primeMemory(r, p);
    r.behaviors.tick(*r.bank, e.behaviors, hmi::twin::now(), e.wordOrder != "fort", plcReader_);
    // Visible sur le vrai reseau : le meme jumeau, sur 0.0.0.0:port.
    if (e.twinExpose && (!r.exposed || r.exposedPort != e.twinExposePort)) {
        if (r.exposed) r.exposed->stop();
        r.exposed = std::make_unique<hmi::modbus::Server>();
        std::string why;
        if (r.exposed->start("0.0.0.0", e.twinExposePort, r.bank, &why)) {
            r.exposed->setServerTap(true);
            r.exposeWhy.clear();
            event("Esclave simul\xC3\xA9 " + e.twinLabel() + " visible sur le vrai r\xC3\xA9seau (port " + std::to_string(e.twinExposePort) + ")");
        } else {
            r.exposeWhy = why;
            event("Esclave simul\xC3\xA9 " + e.twinLabel() + " : port " + std::to_string(e.twinExposePort) + " impossible (" + why + ")");
            r.exposed.reset();
        }
        r.exposedPort = e.twinExposePort;
    } else if (!e.twinExpose && r.exposed) {
        r.exposed->stop();
        r.exposed.reset();
        r.exposedPort = 0;
        event("Esclave simul\xC3\xA9 " + e.twinLabel() + " : plus visible sur le vrai r\xC3\xA9seau");
    } else if (!e.twinExpose) {
        r.exposedPort = 0;
        r.exposeWhy.clear();
    }
    if (r.exposed) {
        r.exposed->setMute(!e.twinResponds);
        r.exposed->setDelay(e.twinDelayMs, e.twinJitterMs);
    }
}

void EquipmentHost::tick(const hmi::Project* p, double dt) {
    clock_ += dt;
    const double now = clock_;
    std::map<hmi::Id, Running> keep;
    if (p) {
        for (const auto& e : p->equipments) {
            auto it = running_.find(e.id);
            Running r;
            if (it != running_.end()) {
                r = std::move(it->second);
                running_.erase(it);
            }
            const bool renamed = !r.equipment.name.empty() && r.equipment.name != e.name;
            r.equipment = e;
            // 1.9 : l'equipement tel que l'esclave le joue (les choix de la page Simulation).
            r.effective = effective(e);
            const hmi::Equipment& eff = r.effective;
            if (renamed) r.reported = false;
            // Lot 17 : son jumeau (l'equipement simule du lot 15 en est un).
            runTwin(r, eff, *p);
            // Sa liaison - 1.9 : au vrai, ou a l'esclave simule (la fiche, la page, la bascule).
            const bool twinUsed = updateSource(r, eff, *p, now);
            const int twinPort = twinUsed && r.server && r.server->running() ? r.server->port() : 0;
            const bool wantLink = e.enabled && e.modbus() && (!twinUsed || twinPort > 0);
            if (wantLink) {
                const auto plan = hmi::equip::buildPlan(*p, eff);
                const auto settings = hmi::equip::settingsOf(eff, twinPort);
                const std::string key = plan.signature() + "|" + settingsKey(settings);
                if (!r.link || key != r.linkKey) {
                    if (r.link) {
                        for (auto& ev : r.link->takeEvents()) event(e.name + " : " + ev);
                        r.link->stop();
                    }
                    const std::size_t n = plan.points().size();
                    r.link = std::make_shared<hmi::comm::Link>(plan, settings);
                    r.link->start();
                    r.linkKey = key;
                    r.linkToSlave = twinPort > 0;
                    r.watchName = plan.points().empty() ? std::string{} : plan.points().front().name;
                    event("\xC3\x89quipement " + e.name + " : liaison Modbus TCP vers " + settings.host + ":" + std::to_string(settings.port) + " (esclave "
                          + std::to_string(settings.unit) + ", " + std::to_string(n) + " variable" + (n > 1 ? "s" : "") + " li\xC3\xA9" "e"
                          + (n > 1 ? "s" : "") + ")");
                }
            } else if (r.link) {
                for (auto& ev : r.link->takeEvents()) event(e.name + " : " + ev);
                r.link->stop();
                r.link.reset();
                r.linkKey.clear();
                r.linkToSlave = false;
                event("\xC3\x89quipement " + e.name + " : liaison arr\xC3\xAAt\xC3\xA9" "e");
            }
            // 1.9 : automatique, sur le vrai - une variable lue en veille. La liaison ne
            // lit que ce qu'on lui demande : sans personne pour lire (pas de vue
            // ouverte), un vrai appareil qui se tait ne se remarquerait pas.
            if (r.link && !r.linkToSlave && !r.readSlave && !r.watchName.empty() && eff.linkedSlave() && sourceOf(eff) == hmi::ReadSource::Auto) {
                sim::Value v;
                (void)r.link->read(r.watchName, v);
            }
            if (r.link)
                for (auto& ev : r.link->takeEvents()) event(e.name + " : " + ev);
            // Son ping.
            if (e.enabled && !e.simulated && e.pingS > 0 && now >= r.nextPing) {
                r.nextPing = now + e.pingS;
                Job j;
                j.kind = Job::Kind::Ping;
                j.key = e.name;
                j.host = e.host;
                j.port = e.port;
                j.timeoutMs = e.timeoutMs;
                j.tryPort = !e.modbus();
                schedule(std::move(j));
            }
            keep.emplace(e.id, std::move(r));
        }
    }
    for (auto& [id, r] : running_) {
        if (r.link) r.link->stop();
        if (r.exposed) r.exposed->stop();
        if (r.server) {
            r.server->stop();
            event("Esclave simul\xC3\xA9 " + r.equipment.twinLabel() + " arr\xC3\xAAt\xC3\xA9");
        }
    }
    running_ = std::move(keep);
    // L'automate du projet : son ping, pour le schema du reseau.
    plc_ = p ? p->comm : hmi::Communication{};
    if (plc_.modbus() && now >= plcNextPing_) {
        plcNextPing_ = now + 10;
        Job j;
        j.kind = Job::Kind::Ping;
        j.key = kPlc;
        j.host = plc_.host;
        j.port = plc_.port;
        j.timeoutMs = plc_.timeoutMs;
        schedule(std::move(j));
    }
    {
        std::vector<std::string> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(workerEvents_);
        }
        for (auto& e : pending) event(std::move(e));
    }
    rebuildStatuses(p);
}

void EquipmentHost::rebuildStatuses(const hmi::Project* p) {
    statuses_.clear();
    if (!p) return;
    std::map<std::string, Probe> probes;
    std::map<std::string, ModbusProbe> mprobes;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        probes = probes_;
        mprobes = modbusProbes_;
    }
    for (const auto& pe : p->equipments) {
        hmi::EquipmentStatus st;
        st.name = pe.name;
        st.type = std::string(hmi::equipmentTypeLabel(pe.type));
        st.enabled = pe.enabled;
        const auto rit = running_.find(pe.id);
        Running* r = rit != running_.end() ? &rit->second : nullptr;
        // 1.9 : l'equipement tel que l'esclave le joue (les choix de la page Simulation).
        const hmi::Equipment& e = r ? r->effective : pe;
        const auto pit = probes.find(e.name);
        const Probe* pr = pit != probes.end() && pit->second.host == e.host ? &pit->second : nullptr;
        if (pr && pr->ok) st.pingMs = pr->ms;
        // Lot 17 : le jumeau et le vrai appareil, chacun son etat.
        st.twin = e.hasTwin();
        st.viaTwin = e.enabled && (r ? r->readSlave : usesTwin(e));
        st.twinName = e.twinLabel();
        // 1.9 : ce que lit l'IHM, et pourquoi.
        if (e.hasTwin() && e.modbus()) {
            st.readMode = std::string(hmi::readSourceKey(sourceOf(e)));
            st.chosen = chosenSource(e.name).has_value();
            st.fallback = r && r->fallback;
            st.realOnline = r && r->realOnline;
            st.readSince = r ? r->readSince : 0;
            st.realSilentSince = r && r->silentSince >= 0 ? r->silentWall : 0;
            if (e.simulated) st.readWhy = "seulement simul\xC3\xA9 : pas encore de vrai appareil";
            else if (st.viaTwin && st.fallback)
                st.readWhy = "le vrai ne r\xC3\xA9pond pas" + (st.realSilentSince > 0 ? " depuis " + clockText(st.realSilentSince) : std::string{});
            else if (st.chosen) st.readWhy = "choisi sur la page Simulation";
            else if (st.viaTwin) st.readWhy = station_ ? "la fiche (sur le poste)" : "la fiche : l'esclave simul\xC3\xA9";
        }
        if (e.hasTwin()) {
            const int port = r && r->server ? r->server->port() : 0;
            st.twinAddress = e.twinAddress() + (port ? " (simul\xC3\xA9, 127.0.0.1:" + std::to_string(port) + ")" : std::string(" (simul\xC3\xA9)"));
            if (!e.modbus()) {
                st.twinState = e.twinPing ? "r\xC3\xA9pond au ping (simul\xC3\xA9)" : "panne simul\xC3\xA9" "e : ne r\xC3\xA9pond plus";
                st.twinTone = e.twinPing ? 1 : 2;
            } else if (r && r->server && r->server->running()) {
                if (!e.twinResponds) {
                    st.twinState = "panne simul\xC3\xA9" "e : ne r\xC3\xA9pond plus";
                    st.twinTone = 2;
                } else if (e.twinException) {
                    st.twinState = "exception " + std::to_string(e.twinException) + " forc\xC3\xA9" "e";
                    st.twinTone = 2;
                } else {
                    const auto s = r->server->stats();
                    st.twinState = "en marche \xC2\xB7 " + std::to_string(s.requests) + " requ\xC3\xAAte" + (s.requests > 1 ? "s" : "");
                    st.twinTone = 1;
                }
                if (r->exposed) st.twinState += " \xC2\xB7 visible (port " + std::to_string(r->exposedPort) + ")";
                else if (e.twinExpose && !r->exposeWhy.empty()) st.twinState += " \xC2\xB7 port " + std::to_string(e.twinExposePort) + " impossible";
            } else {
                st.twinState = e.twinRunning ? std::string("arr\xC3\xAAt\xC3\xA9") : std::string("arr\xC3\xAAt\xC3\xA9 (pas en marche)");
                st.twinTone = 5;
            }
        }
        if (e.simulated) {
            st.realState = "aucun (seulement simul\xC3\xA9)";
        } else if (pr) {
            st.realState = pr->ok ? "joignable (ping " + (pr->ms < 0.1 ? std::string("< 0.1") : std::to_string(static_cast<int>(std::lround(pr->ms)))) + " ms)"
                         : pr->portOk ? std::string("joignable (port)") : "injoignable";
            st.realTone = pr->ok || pr->portOk ? 1 : 3;
        } else {
            st.realState = "pas encore test\xC3\xA9";
        }
        if (!e.enabled) {
            st.state = "D\xC3\xA9sactiv\xC3\xA9";
            st.address = e.host + ":" + portText(e.port);
        } else if (st.viaTwin) {
            st.simulated = true;
            st.address = st.twinAddress;
            st.tested = true;
            if (e.modbus()) {
                st.reachable = r && r->link && r->link->connected();
                st.state = "Simul\xC3\xA9";
                if (!r || !r->server) st.why = "esclave simul\xC3\xA9 arr\xC3\xAAt\xC3\xA9";
                else if (!e.twinResponds) st.why = "panne simul\xC3\xA9" "e";
                // 1.9 : le vrai, pendant la bascule : le dernier essai Modbus.
                if (!e.simulated && st.fallback) {
                    const auto mp = mprobes.find(e.name);
                    if (mp != mprobes.end() && mp->second.done)
                        st.realState = mp->second.ok ? std::string("r\xC3\xA9pond \xC3\xA0 nouveau (essai ") + clockText(mp->second.at) + ")"
                                                     : "ne r\xC3\xA9pond pas (essai " + clockText(mp->second.at) + (mp->second.why.empty() ? std::string{} : " : " + mp->second.why) + ")";
                    else
                        st.realState = "ne r\xC3\xA9pond pas";
                    st.realTone = mp != mprobes.end() && mp->second.ok ? 1 : 3;
                }
            } else {
                st.reachable = e.twinPing;
                st.state = e.twinPing ? "Joignable" : "Injoignable";
                if (!e.twinPing) st.why = "panne simul\xC3\xA9" "e (l'esclave simul\xC3\xA9 ne r\xC3\xA9pond pas au ping)";
                if (e.twinPing) st.pingMs = 0.05;
            }
        } else if (e.modbus()) {
            st.address = e.host + ":" + portText(e.port) + " (esclave " + std::to_string(e.unit) + ")";
            if (r && r->link) {
                const auto d = r->link->diagnostics();
                st.reachable = d.connected;
                st.tested = d.connected || d.requests > 0 || d.reconnects > 0 || !d.lastError.empty() || pr != nullptr;
                st.state = d.connected ? "Connect\xC3\xA9" : d.lastError.empty() ? "Connexion..." : "Injoignable";
                if (!d.connected) {
                    st.why = d.lastError;
                    if (pr && pr->ok && !st.why.empty())
                        st.why += " (le ping r\xC3\xA9pond : le port " + portText(e.port) + " est-il le bon ? le serveur Modbus est-il activ\xC3\xA9 ?)";
                    else if (pr && !pr->ok && st.why.empty())
                        st.why = "ping : " + pr->why;
                }
                if (d.connected) {
                    st.realState = "connect\xC3\xA9" + std::string(pr && pr->ok ? " (ping " + std::to_string(static_cast<int>(std::lround(pr->ms))) + " ms)" : "");
                    st.realTone = 1;
                } else if (!d.lastError.empty()) {
                    st.realState = "injoignable";
                    st.realTone = 3;
                }
                // 1.9 : automatique - le vrai se tait, la bascule approche.
                if (!d.connected && st.readMode == "auto" && r->silentSince >= 0) {
                    const int left = std::max(0, e.fallbackAfterS - static_cast<int>(clock_ - r->silentSince));
                    st.readWhy = "le vrai ne r\xC3\xA9pond pas depuis " + clockText(r->silentWall) + " : l'esclave dans " + std::to_string(left) + " s";
                }
            } else {
                st.state = "Pas encore test\xC3\xA9";
            }
        } else {
            st.address = e.host + (e.port > 0 ? ":" + portText(e.port) : std::string{});
            if (pr) {
                st.tested = true;
                st.reachable = pr->ok || pr->portOk;
                st.state = st.reachable ? "Joignable" : "Injoignable";
                if (!st.reachable) st.why = "ping : " + pr->why + (pr->portTried ? " ; port " + portText(e.port) + " : " + pr->portWhy : std::string{});
            } else {
                st.state = "Pas encore test\xC3\xA9";
            }
            // Au journal : injoignable, puis joignable a nouveau.
            if (r && st.tested && (!r->reported || r->lastReachable != st.reachable)) {
                if (r->reported || !st.reachable)
                    event("\xC3\x89quipement " + e.name + (st.reachable ? " : joignable \xC3\xA0 nouveau" : " : injoignable (" + st.why + ")"));
                r->reported = true;
                r->lastReachable = st.reachable;
            }
        }
        statuses_.push_back(std::move(st));
    }
}

hmi::comm::Link* EquipmentHost::link(std::string_view equipment) const {
    if (equipment.empty()) return nullptr;
    for (const auto& [id, r] : running_)
        if (r.link && sameName(r.equipment.name, equipment)) return r.link.get();
    return nullptr;
}

std::optional<hmi::EquipmentStatus> EquipmentHost::status(std::string_view equipment) const {
    for (const auto& st : statuses_)
        if (sameName(st.name, equipment)) return st;
    return std::nullopt;
}

int EquipmentHost::simulatedPort(std::string_view equipment) const {
    for (const auto& [id, r] : running_)
        if (r.server && r.server->running() && sameName(r.equipment.name, equipment)) return r.server->port();
    return 0;
}

std::shared_ptr<hmi::twin::TwinBank> EquipmentHost::twinBank(std::string_view equipment) const {
    for (const auto& [id, r] : running_)
        if (r.bank && sameName(r.equipment.name, equipment)) return r.bank;
    return nullptr;
}

hmi::modbus::Server::Stats EquipmentHost::simulatedStats(std::string_view equipment) const {
    for (const auto& [id, r] : running_)
        if (r.server && sameName(r.equipment.name, equipment)) return r.server->stats();
    return {};
}

bool EquipmentHost::viaTwin(std::string_view equipment) const {
    for (const auto& [id, r] : running_)
        if (sameName(r.equipment.name, equipment)) return r.equipment.enabled && (r.equipment.modbus() ? r.readSlave : usesTwin(r.equipment));
    return false;
}

std::string EquipmentHost::twinOfEndpoint(const std::string& endpoint) const {
    for (const auto& [id, r] : running_) {
        if (!r.server || !r.server->running()) continue;
        const std::string mine = "127.0.0.1:" + std::to_string(r.server->port());
        if (endpoint == mine) return r.equipment.twinLabel();
        if (r.exposed && endpoint.size() > 6 && endpoint.rfind(":" + std::to_string(r.exposedPort)) == endpoint.size() - std::to_string(r.exposedPort).size() - 1)
            return r.equipment.twinLabel() + " (visible)";
    }
    return {};
}

void EquipmentHost::restartTwin(std::string_view equipment) {
    for (auto& [id, r] : running_)
        if (sameName(r.equipment.name, equipment)) r.restart = true;
}

std::optional<EquipmentHost::Probe> EquipmentHost::probe(std::string_view equipment) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, p] : probes_)
        if (key == equipment || sameName(key, equipment)) return p;
    return std::nullopt;
}

void EquipmentHost::test(std::string_view equipment) {
    for (auto& [id, r] : running_) {
        const auto& e = r.equipment;
        if (!e.enabled || (e.simulated && e.modbus())) continue;
        if (!equipment.empty() && !sameName(e.name, equipment)) continue;
        Job j;
        j.kind = Job::Kind::Ping;
        j.key = e.name;
        j.host = e.host;
        j.port = e.port;
        j.timeoutMs = e.timeoutMs;
        j.tryPort = !e.modbus();
        r.nextPing = clock_ + std::max(1, e.pingS);
        schedule(std::move(j));
        if (r.link && !r.link->connected()) r.link->reconnect();
    }
    if (equipment.empty() && plc_.modbus()) {
        Job j;
        j.kind = Job::Kind::Ping;
        j.key = kPlc;
        j.host = plc_.host;
        j.port = plc_.port;
        j.timeoutMs = plc_.timeoutMs;
        plcNextPing_ = clock_ + 10;
        schedule(std::move(j));
    }
}

bool EquipmentHost::testing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (busy_ > 0) return true;
    return std::any_of(jobs_.begin(), jobs_.end(), [](const Job& j) { return j.kind == Job::Kind::Ping; });
}

double EquipmentHost::testedAt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return testedAt_;
}

void EquipmentHost::pingAddress(const std::string& host, int port, int timeoutMs) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        manualPing_.reset();
    }
    Job j;
    j.kind = Job::Kind::Free;
    j.host = host;
    j.port = port;
    j.timeoutMs = timeoutMs;
    schedule(std::move(j));
}

std::optional<EquipmentHost::Probe> EquipmentHost::pingResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manualPing_;
}

void EquipmentHost::reconnect(std::string_view equipment) {
    if (auto* lk = link(equipment)) lk->reconnect();
}

std::vector<hmi::netinfo::Adapter> EquipmentHost::adapters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adapters_;
}

bool EquipmentHost::adaptersKnown() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adaptersKnown_;
}

double EquipmentHost::adaptersAt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adaptersAt_;
}

void EquipmentHost::refreshAdapters() {
    Job j;
    j.kind = Job::Kind::Adapters;
    schedule(std::move(j));
}

std::string EquipmentHost::computerName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return computer_;
}

std::string EquipmentHost::systemName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return system_;
}

void EquipmentHost::checkAddress(std::uint32_t ip) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (addressCheck_.ip == ip && !addressCheck_.done) return;
        addressCheck_ = AddressCheck{};
        addressCheck_.ip = ip;
    }
    Job j;
    j.kind = Job::Kind::Address;
    j.ip = ip;
    schedule(std::move(j));
}

EquipmentHost::AddressCheck EquipmentHost::addressCheck() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return addressCheck_;
}

bool EquipmentHost::applyPort(const hmi::netinfo::Adapter& adapter, const hmi::netinfo::PortConfig& config, std::string* why) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (apply_.busy) {
            if (why) *why = "un changement d'adresse est d\xC3\xA9j\xC3\xA0 en cours (" + apply_.adapter + ")";
            return false;
        }
    }
    if (applyThread_.joinable()) applyThread_.join();
    const auto before = hmi::netinfo::currentConfig(adapter);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apply_ = ApplyState{};
        apply_.busy = true;
        apply_.adapter = adapter.name;
        apply_.config = config;
        apply_.before = before;
    }
    applyThread_ = std::thread([this, adapter, config, before] {
        std::string log, reason;
        const bool ok = hmi::netinfo::applyPortConfig(adapter, config, &log, &reason);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            apply_.busy = false;
            apply_.done = true;
            apply_.ok = ok;
            apply_.why = reason;
            apply_.log = log;
            apply_.at = wallNow();
            if (ok) {
                previous_[adapter.name] = before;
                lastChanged_ = adapter.name;
            }
            workerEvents_.push_back(ok ? "Port " + adapter.name + " : "
                                             + (config.dhcp ? std::string("adresse automatique (DHCP)") : "adresse " + config.ip + " / " + config.mask)
                                             + " appliqu\xC3\xA9" "e"
                                       : "Port " + adapter.name + " : changement d'adresse refus\xC3\xA9 (" + reason + ")");
        }
        Job j;
        j.kind = Job::Kind::Adapters;
        schedule(std::move(j));
    });
    return true;
}

EquipmentHost::ApplyState EquipmentHost::applyState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return apply_;
}

std::optional<hmi::netinfo::PortConfig> EquipmentHost::previous(std::string_view adapter) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [name, cfg] : previous_)
        if (name == adapter) return cfg;
    return std::nullopt;
}

std::string EquipmentHost::lastChangedAdapter() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastChanged_;
}

// ============================================== 1.9 : l'esclave simule lie ===
EquipmentHost::Choice* EquipmentHost::choice(std::string_view equipment) {
    const auto it = choices_.find(upperOf(equipment));
    return it == choices_.end() ? nullptr : &it->second;
}

const EquipmentHost::Choice* EquipmentHost::choice(std::string_view equipment) const {
    const auto it = choices_.find(upperOf(equipment));
    return it == choices_.end() ? nullptr : &it->second;
}

const EquipmentHost::Running* EquipmentHost::runningOf(std::string_view equipment) const {
    for (const auto& [id, r] : running_)
        if (sameName(r.equipment.name, equipment)) return &r;
    return nullptr;
}

EquipmentHost::Running* EquipmentHost::runningOf(std::string_view equipment) {
    for (auto& [id, r] : running_)
        if (sameName(r.equipment.name, equipment)) return &r;
    return nullptr;
}

std::optional<hmi::ReadSource> EquipmentHost::chosenSource(std::string_view equipment) const {
    if (const auto* c = choice(equipment)) return c->source;
    return std::nullopt;
}

hmi::ReadSource EquipmentHost::sourceOf(const hmi::Equipment& e) const {
    if (e.simulated) return hmi::ReadSource::Slave;
    if (!e.hasTwin()) return hmi::ReadSource::Real;
    // Sur le poste, "toujours le vrai" ne se change pas, meme par la page.
    if (station_ && e.stationRead == hmi::StationRead::Real) return hmi::ReadSource::Real;
    if (const auto chosen = chosenSource(e.name)) return *chosen;
    if (station_) return e.stationRead == hmi::StationRead::Auto ? hmi::ReadSource::Auto : hmi::ReadSource::Real;
    return e.appRead();
}

hmi::Equipment EquipmentHost::effective(const hmi::Equipment& e) const {
    hmi::Equipment out = e;
    if (const auto* c = choice(e.name)) {
        if (c->running) out.twinRunning = *c->running;
        if (c->responds) out.twinResponds = *c->responds;
        if (c->exception) out.twinException = *c->exception;
        if (c->behaviors) out.behaviors = *c->behaviors;
        if (c->forcings) out.forcings = *c->forcings;
    }
    return out;
}

void EquipmentHost::clearRuntimeChoices() {
    // Les choix de la page Simulation s'oublient : l'IHM relit ce que dit la fiche.
    // La bascule automatique, elle, dit ce qui est (le vrai se tait) : elle reste -
    // l'oublier ferait relire un vrai muet pendant "basculer apres" secondes.
    choices_.clear();
}

void EquipmentHost::switchSource(Running& r, const hmi::Equipment& e, bool slave, bool fallback, const std::string& why) {
    const bool changed = r.readSlave != slave;
    const bool wasFallback = r.fallback;
    if (changed) r.readSince = wallNow();
    r.readSlave = slave;
    r.fallback = slave && fallback;
    // Le journal de l'IHM le dit (Runtime::slaveWatch : un evenement, une marque
    // sur les courbes) ; ici, l'avis de la cloche et le message du volet - pas
    // pour la premiere source de l'equipement (au demarrage, rien n'a bascule).
    if (!changed || !r.sourceKnown || e.simulated) return;
    SourceSwitch s;
    s.seq = ++switchSeq_;
    s.equipment = e.name;
    s.toSlave = slave;
    s.fallback = slave && fallback;
    s.chosen = chosenSource(e.name).has_value();
    s.at = r.readSince;
    s.silentSince = r.silentSince >= 0 ? r.silentWall : 0;
    s.afterS = std::max(1, e.fallbackAfterS);
    const char* const simulatedValues = " Les valeurs \xC3\xA0 l'\xC3\xA9" "cran sont simul\xC3\xA9" "es.";
    if (slave && fallback)
        s.text = "Il ne r\xC3\xA9pond plus depuis " + std::to_string(s.afterS) + " s : l'IHM lit maintenant son esclave simul\xC3\xA9." + simulatedValues;
    else if (slave)
        s.text = "L'IHM lit maintenant son esclave simul\xC3\xA9 (" + (s.chosen ? std::string("choisi sur la page Simulation") : std::string("sa fiche le dit")) + ")."
                 + simulatedValues;
    else if (wasFallback && why == "retour")
        s.text = "Il r\xC3\xA9pond de nouveau : l'IHM lit maintenant le vrai appareil.";
    else
        s.text = "L'IHM lit de nouveau le vrai appareil (" + (why == "page" || s.chosen ? std::string("page Simulation") : std::string("sa fiche le dit")) + ").";
    switches_.push_back(std::move(s));
    while (switches_.size() > 64) switches_.pop_front();
}

std::vector<EquipmentHost::SourceSwitch> EquipmentHost::switchesAfter(std::uint64_t seq) const {
    std::vector<SourceSwitch> out;
    for (const auto& s : switches_)
        if (s.seq > seq) out.push_back(s);
    return out;
}

void EquipmentHost::scheduleProbe(Running& r, const hmi::Equipment& e, const hmi::Project& p) {
    Job j;
    j.kind = Job::Kind::Modbus;
    j.key = e.name;
    j.host = e.host;
    j.port = e.port;
    j.unit = e.unit;
    j.timeoutMs = e.timeoutMs;
    // La premiere variable liee (sa table, sa case) ; sinon le registre 40001.
    const auto plan = hmi::equip::buildPlan(p, e);
    if (!plan.points().empty()) {
        const auto& pt = plan.points().front();
        // comm::Area : bobines 0, entrees TOR 1, maintien 2, entrees 3 - les codes de Job::area.
        j.area = static_cast<int>(pt.area);
        j.offset = pt.offset;
    }
    r.probeAsked = wallNow();
    schedule(std::move(j));
}

bool EquipmentHost::updateSource(Running& r, const hmi::Equipment& e, const hmi::Project& p, double now) {
    // La premiere source de l'equipement se pose sans avis (rien n'a bascule).
    struct Known {
        Running& r;
        ~Known() { r.sourceKnown = true; }
    } known{r};
    const bool linkToReal = r.link && !r.linkToSlave;
    if (!e.enabled || !e.hasTwin()) {
        r.readSlave = r.fallback = false;
        r.silentSince = -1;
        r.probeOk = 0;
        r.realOnline = linkToReal && r.link->connected();
        return false;
    }
    if (!e.modbus()) {                       // Ethernet TCP/IP : le jumeau repond au ping (lot 17)
        r.readSlave = usesTwin(e);
        r.fallback = false;
        return r.readSlave;
    }
    if (e.simulated) {                       // seulement simule : il n'y a que lui
        if (!r.readSlave) switchSource(r, e, true, false, {});
        r.realOnline = false;
        return true;
    }
    const hmi::ReadSource mode = sourceOf(e);
    const bool slaveUp = r.server && r.server->running();
    if (mode == hmi::ReadSource::Real) {
        if (r.readSlave) switchSource(r, e, false, false, {});
        r.silentSince = -1;
        r.probeOk = 0;
        r.realOnline = linkToReal && r.link->connected();
        return false;
    }
    if (mode == hmi::ReadSource::Slave) {
        if (!r.readSlave || r.fallback) switchSource(r, e, true, false, {});
        r.silentSince = -1;
        return true;
    }
    // ---- automatique
    // L'esclave par choix (la fiche, la page), puis "automatique" : on repart du
    // vrai ; s'il se tait, la bascule suit son cours (et le dit).
    if (r.readSlave && !r.fallback) {
        switchSource(r, e, false, false, {});
        r.silentSince = -1;
        r.probeOk = 0;
        r.probeAsked = 0;
        return false;
    }
    if (!r.readSlave) {
        const bool online = linkToReal && r.link->connected();
        r.realOnline = online;
        if (online) {
            r.silentSince = -1;
            return false;
        }
        if (r.silentSince < 0) {
            r.silentSince = now;
            r.silentWall = wallNow();
        }
        if (slaveUp && now - r.silentSince >= std::max(1, e.fallbackAfterS)) {
            switchSource(r, e, true, true, {});
            r.probeOk = 0;
            r.probeAsked = 0;
            r.nextProbe = now + probeEvery_;
            return true;
        }
        return false;
    }
    // L'esclave, par la bascule automatique : le vrai est essaye toutes les N s.
    r.fallback = true;
    if (r.silentSince < 0) {
        r.silentSince = now;
        r.silentWall = wallNow();
    }
    if (r.probeAsked > 0) {
        std::optional<ModbusProbe> got;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (const auto it = modbusProbes_.find(e.name); it != modbusProbes_.end() && it->second.done && it->second.at >= r.probeAsked)
                got = it->second;
        }
        if (got) {
            r.probeAsked = 0;
            r.realOnline = got->ok;
            r.probeOk = got->ok ? r.probeOk + 1 : 0;
        }
    }
    if (e.fallbackReturn && r.probeOk >= 2) {
        switchSource(r, e, false, false, "retour");
        r.silentSince = -1;
        r.probeOk = 0;
        return false;
    }
    if (r.probeAsked <= 0 && now >= r.nextProbe) {
        scheduleProbe(r, e, p);
        r.nextProbe = now + probeEvery_;
    }
    return true;
}

std::vector<hmi::SimSlave> EquipmentHost::simSlaves(const hmi::Project& p, bool withValues) const {
    std::vector<hmi::SimSlave> out;
    for (const auto& pe : p.equipments) {
        if (!pe.modbus() || !pe.hasTwin()) continue;
        const Running* r = runningOf(pe.name);
        const hmi::Equipment e = r ? r->effective : effective(pe);
        hmi::SimSlave s;
        s.equipment = e.name;
        s.name = e.twinLabel();
        s.key = hmi::slaveKey(e.name);
        s.linked = e.linkedSlave();
        s.enabled = e.enabled;
        s.running = r && r->server && r->server->running();
        s.responds = e.twinResponds;
        s.exception = e.twinException;
        s.read = e.enabled && r && r->readSlave;
        s.fallback = s.read && r->fallback;
        s.chosen = chosenSource(e.name).has_value();
        s.mode = std::string(hmi::readSourceKey(sourceOf(e)));
        if (e.simulated) {
            s.modeLocked = true;
            s.modeWhy = "seulement simul\xC3\xA9 : pas encore de vrai appareil, l'IHM le lit toujours";
        } else if (station_ && e.stationRead == hmi::StationRead::Real) {
            s.modeLocked = true;
            s.modeWhy = "sur ce poste, sa fiche dit : toujours le vrai appareil";
        }
        s.realOnline = r && r->realOnline;
        s.realSilentSince = r && r->silentSince >= 0 ? r->silentWall : 0;
        s.readSince = r ? r->readSince : 0;
        s.port = s.running ? r->server->port() : 0;
        if (r && r->server) {
            const auto st = r->server->stats();
            s.requests = st.requests;
            s.clients = static_cast<int>(st.clients);
        }
        if (r && r->bank) s.refusedWrites = r->bank->counters().forcedRefused;
        s.responseMs = e.twinDelayMs;
        for (const auto& b : e.behaviors) s.animated += b.enabled ? 1 : 0;
        s.forced = static_cast<int>(e.forcings.size());
        if (!e.enabled) s.state = "\xC3\xA9quipement d\xC3\xA9sactiv\xC3\xA9";
        else if (!s.running) s.state = e.twinRunning ? "arr\xC3\xAAt\xC3\xA9" : "arr\xC3\xAAt\xC3\xA9 (pas en marche)";
        else if (!e.twinResponds) s.state = "panne simul\xC3\xA9" "e : ne r\xC3\xA9pond plus";
        else if (e.twinException) s.state = "exception " + std::to_string(e.twinException) + " forc\xC3\xA9" "e";
        else s.state = "en marche \xC2\xB7 " + std::to_string(s.requests) + " requ\xC3\xAAte" + (s.requests > 1 ? "s" : "");
        if (e.simulated) s.why = "seulement simul\xC3\xA9 : l'IHM le lit";
        else if (s.read && s.fallback) s.why = "l'IHM le lit : le vrai ne r\xC3\xA9pond pas" + (s.realSilentSince > 0 ? " depuis " + clockText(s.realSilentSince) : std::string{});
        else if (s.read) s.why = s.chosen ? "l'IHM le lit (choisi sur la page Simulation)" : "l'IHM le lit (la fiche)";
        else s.why = "pr\xC3\xAAt, l'IHM lit le vrai";
        if (withValues) {
            const bool low = e.wordOrder != "fort";
            for (const auto& row : hmi::twin::valueRows(p, e)) {
                hmi::SimSlaveValue v;
                v.key = row.key();
                v.variable = row.variable;
                v.address = row.address;
                v.type = row.type;
                v.boolean = row.boolean;
                for (const auto& d : p.displays)
                    if (!row.root.empty() && sameName(d.path, row.root)) v.unit = d.unit;
                v.note = row.variable.empty() ? std::string("registre sans variable")
                         : row.written ? std::string("\xC3\xA9" "crite par l'IHM")
                         : row.bit >= 0 ? std::string("bit d'un mot") : std::string("lue par l'IHM");
                if (row.behavior >= 0 && row.behavior < static_cast<int>(e.behaviors.size())) {
                    const auto& b = e.behaviors[static_cast<std::size_t>(row.behavior)];
                    v.animated = b.enabled;
                    v.kind = std::string(hmi::behaviorKindKey(b.kind));
                    v.kindLabel = std::string(hmi::behaviorKindLabel(b.kind));
                    v.low = row.eng(b.a);
                    v.high = row.eng(b.b);
                    v.period = b.period;
                } else {
                    v.kindLabel = "aucun";
                    const auto [lo, hi] = hmi::twin::barRange(row, nullptr, std::nullopt);
                    v.low = lo;
                    v.high = hi;
                }
                v.scaled = row.scaled;                                   // 1.9 : la page Simulation
                if (row.forcing >= 0 && row.forcing < static_cast<int>(e.forcings.size())) {
                    v.forced = true;
                    v.forcedValue = e.forcings[static_cast<std::size_t>(row.forcing)].value;
                    v.forcedText = hmi::twin::numberText(v.forcedValue);
                    v.forcedNumber = row.boolean ? v.forcedValue : row.eng(v.forcedValue);
                }
                if (r && r->bank) {
                    if (const auto raw = hmi::twin::rowValue(*r->bank, row, low)) {
                        v.number = row.eng(*raw);
                        v.value = row.boolean ? (*raw != 0 ? std::string("1") : std::string("0")) : hmi::twin::numberText(v.number);
                    }
                }
                s.values.push_back(std::move(v));
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

bool EquipmentHost::simSlaveCommand(const hmi::Project& p, const hmi::SimSlaveCommand& c, std::string* why) {
    const auto fail = [&](std::string m) {
        if (why) *why = std::move(m);
        return false;
    };
    const auto truth = [](const std::string& v) {
        std::string l = upperOf(v);
        return l == "1" || l == "TRUE" || l == "OUI" || l == "VRAI" || l == "ON";
    };
    namespace tw = hmi::twin;
    // Tous les esclaves : animer, arreter, deforcer.
    if (c.equipment.empty() && (c.what == "tout_animer" || c.what == "tout_arreter" || c.what == "deforcer_tout")) {
        bool any = false;
        for (const auto& e : p.equipments)
            if (e.modbus() && e.hasTwin()) {
                hmi::SimSlaveCommand one = c;
                one.equipment = e.name;
                any = simSlaveCommand(p, one, nullptr) || any;
            }
        if (!any) return fail("aucun esclave simul\xC3\xA9");
        return true;
    }
    const hmi::Equipment* pe = p.equipmentByName(c.equipment);
    if (!pe || !pe->modbus() || !pe->hasTwin()) return fail("pas d'esclave simul\xC3\xA9 " + c.equipment);
    auto& ch = choices_[upperOf(pe->name)];
    const hmi::Equipment cur = effective(*pe);
    const auto rows = tw::valueRows(p, cur);
    const auto rowOf = [&](const std::string& key) -> const tw::ValueRow* {
        for (const auto& r : rows)
            if (r.key() == key || tw::sameCell(r.address, key) || (!r.variable.empty() && sameName(r.variable, key))) return &r;
        return nullptr;
    };
    if (c.what == "source") {
        const auto src = hmi::readSourceFrom(c.value);
        if (!src) return fail("source \xC2\xAB " + c.value + " \xC2\xBB : vrai, esclave ou auto");
        if (pe->simulated) return fail(pe->name + " est seulement simul\xC3\xA9 : l'IHM le lit toujours");
        if (station_ && pe->stationRead == hmi::StationRead::Real)
            return fail("sur ce poste, la fiche de " + pe->name + " dit : toujours le vrai appareil");
        ch.source = *src;
        if (Running* r = runningOf(pe->name)) {
            r->probeOk = 0;
            r->probeAsked = 0;
            if (*src != hmi::ReadSource::Auto) r->silentSince = -1;
        }
        return true;
    }
    if (c.what == "revenir") {
        // Le vrai, maintenant : la page ne choisit plus, la bascule repart de zero.
        ch.source.reset();
        if (Running* r = runningOf(pe->name)) {
            if (r->readSlave && !pe->simulated) switchSource(*r, *pe, false, false, "page");
            r->silentSince = -1;
            r->probeOk = 0;
            r->probeAsked = 0;
            // Une fiche "l'esclave" : le vrai quand meme, jusqu'a un autre choix.
            if (sourceOf(*pe) == hmi::ReadSource::Slave && !pe->simulated) ch.source = hmi::ReadSource::Real;
        }
        return true;
    }
    if (c.what == "marche") {
        ch.running = truth(c.value);
        return true;
    }
    if (c.what == "repond") {
        ch.responds = truth(c.value);
        return true;
    }
    if (c.what == "exception") {
        const int code = std::atoi(c.value.c_str());
        if (code != 0 && code != 1 && code != 2 && code != 3 && code != 4 && code != 6 && code != 11)
            return fail("exception " + c.value + " : 0 (aucune), 1, 2, 3, 4, 6 ou 11");
        ch.exception = code;
        return true;
    }
    // Les valeurs : sur une copie des mouvements et des forcages en vigueur.
    std::vector<hmi::Behavior> behaviors = cur.behaviors;
    std::vector<hmi::Forcing> forcings = cur.forcings;
    const auto lowFirst = cur.wordOrder != "fort";
    Running* run = runningOf(pe->name);
    const auto nowRaw = [&](const tw::ValueRow& row) -> std::optional<double> {
        if (run && run->bank) return tw::rowValue(*run->bank, row, lowFirst);
        return std::nullopt;
    };
    const auto behaviorOf = [&](const tw::ValueRow& row) -> hmi::Behavior* {
        for (auto& b : behaviors)
            if (tw::sameCell(b.address, row.address)) return &b;
        return nullptr;
    };
    if (c.what == "tout_animer" || c.what == "tout_arreter") {
        const bool on = c.what == "tout_animer";
        for (const auto& row : rows) {
            if (row.forcing >= 0) continue;               // une case forcee reste tenue
            if (auto* b = behaviorOf(row)) b->enabled = on;
            else if (on) behaviors.push_back(tw::defaultBehavior(row, nowRaw(row)));
        }
        ch.behaviors = std::move(behaviors);
        return true;
    }
    if (c.what == "deforcer_tout") {
        ch.forcings = std::vector<hmi::Forcing>{};
        return true;
    }
    const tw::ValueRow* row = rowOf(c.key);
    if (!row) return fail(pe->twinLabel() + " : pas de ligne " + c.key);
    if (c.what == "animer") {
        const bool on = truth(c.value);
        if (auto* b = behaviorOf(*row)) b->enabled = on;
        else if (on) behaviors.push_back(tw::defaultBehavior(*row, nowRaw(*row)));
        ch.behaviors = std::move(behaviors);
        return true;
    }
    if (c.what == "mouvement") {
        const std::string v = c.value;
        if (v.empty() || upperOf(v) == "AUCUN") {
            std::erase_if(behaviors, [&](const hmi::Behavior& b) { return tw::sameCell(b.address, row->address); });
            ch.behaviors = std::move(behaviors);
            return true;
        }
        const auto kind = hmi::behaviorKindFrom(v);
        if (!kind) return fail("mouvement \xC2\xAB " + v + " \xC2\xBB inconnu");
        hmi::Behavior* b = behaviorOf(*row);
        if (!b) {
            behaviors.push_back(tw::defaultBehavior(*row, nowRaw(*row)));
            b = &behaviors.back();
        }
        b->kind = *kind;
        b->enabled = true;
        if (*kind == hmi::BehaviorKind::Blink && b->period <= 0) b->period = 4;
        if (*kind == hmi::BehaviorKind::Counter && b->b <= 0) b->b = 1;
        if (!tw::validBehavior(*b)) {
            // Un mouvement qui demande une source (recopie, automate) : sans elle, une constante.
            if (*kind == hmi::BehaviorKind::Copy || *kind == hmi::BehaviorKind::FollowPlc || *kind == hmi::BehaviorKind::Steps)
                return fail("le mouvement \xC2\xAB " + std::string(hmi::behaviorKindLabel(*kind)) + " \xC2\xBB demande une source : r\xC3\xA9glez-le dans Valeurs simul\xC3\xA9" "es");
        }
        ch.behaviors = std::move(behaviors);
        return true;
    }
    if (c.what == "zone") {
        const auto semi = c.value.find(';');
        if (semi == std::string::npos) return fail("zone : \xC2\xAB min;max \xC2\xBB");
        char* end = nullptr;
        const double lo = std::strtod(c.value.substr(0, semi).c_str(), &end);
        const double hi = std::strtod(c.value.substr(semi + 1).c_str(), &end);
        hmi::Behavior* b = behaviorOf(*row);
        if (!b) {
            behaviors.push_back(tw::defaultBehavior(*row, nowRaw(*row)));
            b = &behaviors.back();
        }
        b->a = row->raw(std::min(lo, hi));
        b->b = row->raw(std::max(lo, hi));
        ch.behaviors = std::move(behaviors);
        return true;
    }
    if (c.what == "periode") {
        char* end = nullptr;
        const double s = std::strtod(c.value.c_str(), &end);
        if (!(s > 0) || s > 86400) return fail("p\xC3\xA9riode : de 0,1 \xC3\xA0 86400 s");
        hmi::Behavior* b = behaviorOf(*row);
        if (!b) {
            behaviors.push_back(tw::defaultBehavior(*row, nowRaw(*row)));
            b = &behaviors.back();
        }
        b->period = s;
        ch.behaviors = std::move(behaviors);
        return true;
    }
    if (c.what == "forcer_valeur") {
        // 1.9 : la page Simulation force en valeur de la variable (la mise a l'echelle
        // ramene au brut, arrondi pour un registre entier) ; vide : deforcer.
        hmi::SimSlaveCommand raw = c;
        raw.what = "forcer";
        if (!c.value.empty()) {
            std::string t = c.value;
            std::replace(t.begin(), t.end(), ',', '.');
            char* end = nullptr;
            const double eng = std::strtod(t.c_str(), &end);
            if (t.empty() || !end || *end || !std::isfinite(eng)) return fail("valeur \xC2\xAB " + c.value + " \xC2\xBB illisible");
            double value = row->boolean ? (eng != 0 ? 1.0 : 0.0) : row->raw(eng);
            const std::string type = upperOf(row->type);
            if (type != "REAL" && type != "LREAL") value = std::round(value);
            raw.value = tw::numberText(value);
        }
        return simSlaveCommand(p, raw, why);
    }
    if (c.what == "forcer") {
        std::erase_if(forcings, [&](const hmi::Forcing& f) { return tw::sameCell(f.address, row->address); });
        if (!c.value.empty() && upperOf(c.value) != "LIBRE") {
            double raw = 0;
            std::string reason;
            if (!tw::parseRaw(*row, c.value, raw, &reason)) return fail(reason.empty() ? "valeur \xC2\xAB " + c.value + " \xC2\xBB illisible" : reason);
            hmi::Forcing f;
            f.address = row->address;
            f.type = row->type;
            f.value = raw;
            f.since = hmi::nowStamp();
            forcings.push_back(std::move(f));
        }
        ch.forcings = std::move(forcings);
        return true;
    }
    return fail("commande inconnue : " + c.what);
}

} // namespace app
