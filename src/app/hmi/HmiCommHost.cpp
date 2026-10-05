// app/hmi/HmiCommHost.cpp - la liaison Modbus TCP et le serveur de demonstration (lot 14).
#include "HmiCommHost.hpp"

#include "../../domain/ProjectModel.hpp"
#include "../../hmi/HmiExpr.hpp"
#include "../../sim/Runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace app {

namespace {

hmi::comm::TypeOracle oracleOf(sim::Runtime* rt) {
    if (!rt) return {};
    return [rt](std::string_view path, sim::Type& type) {
        sim::Value v;
        if (!rt->get(path, v)) return false;
        type = v.type();
        return type != sim::Type::Unknown;
    };
}

std::string settingsKey(const hmi::comm::Settings& s) {
    return s.host + ":" + std::to_string(s.port) + "/" + std::to_string(s.unit) + "/" + std::to_string(s.timeoutMs) + "/"
           + std::to_string(s.periodMs) + "/" + std::to_string(s.retryS) + "/" + (s.lowWordFirst ? "f" : "F") + "/"
           + std::to_string(s.maxWords) + "/" + std::to_string(s.maxBits) + "/" + std::to_string(s.gap) + "/" + (s.writes ? "w" : "r") + "/"
           + std::to_string(s.badAfterS);
}

long long msSince(std::chrono::steady_clock::time_point t0) {
    return static_cast<long long>(std::lround(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count()));
}

} // namespace

CommHost::~CommHost() { shutdown(); }

void CommHost::shutdown() {
    if (link_) link_->stop();
    link_.reset();
    if (demo_) demo_->stop();
    demo_.reset();
    bank_.reset();
    seen_ = false;
}

void CommHost::event(std::string text) {
    events_.push_back(std::move(text));
    if (events_.size() > 200) events_.erase(events_.begin(), events_.begin() + 100);
}

std::vector<std::string> CommHost::takeEvents() {
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

hmi::modbus::Server::Stats CommHost::demoStats() const { return demo_ ? demo_->stats() : hmi::modbus::Server::Stats{}; }

void CommHost::setDemoMute(bool on) {
    if (!demo_) return;
    if (demo_->mute() != on) event(on ? "Serveur de d\xC3\xA9monstration muet : il ne r\xC3\xA9pond plus (c\xC3\xA2" "ble d\xC3\xA9" "branch\xC3\xA9)"
                                      : "Serveur de d\xC3\xA9monstration : il r\xC3\xA9pond \xC3\xA0 nouveau");
    demo_->setMute(on);
}

hmi::comm::Plan CommHost::planFor(const hmi::Communication& comm, const domain::Project* plc, sim::Runtime* rt) {
    return hmi::comm::buildPlan(plc, comm, oracleOf(rt));
}

void CommHost::tick(const hmi::Project* hmi, const std::shared_ptr<const domain::Project>& plcRef, sim::Runtime* rt, double dt) {
    const domain::Project* plc = plcRef.get();
    const hmi::Communication comm = hmi ? hmi->comm : hmi::Communication{};
    const bool changed = !seen_ || !(comm == comm_) || plc != plc_ || rt != rt_;
    if (changed) {
        seen_ = true;
        comm_ = comm;
        plc_ = plc;
        rt_ = rt;
        std::optional<hmi::comm::Plan> plan;
        if (comm.modbus() || comm.demoServer) plan = planFor(comm, plc, rt);
        // La liaison.
        if (comm.modbus()) {
            const auto settings = hmi::comm::settingsOf(comm);
            const std::string key = plan->signature() + "|" + settingsKey(settings);
            if (!link_ || key != linkKey_) {
                if (link_) {
                    for (auto& e : link_->takeEvents()) event(std::move(e));
                    link_->stop();
                }
                const std::size_t n = plan->points().size();
                link_ = std::make_shared<hmi::comm::Link>(*plan, settings);
                link_->start();
                linkKey_ = key;
                event("Liaison Modbus TCP vers " + settings.host + ":" + std::to_string(settings.port) + " (esclave " + std::to_string(settings.unit)
                      + ", " + std::to_string(n) + " variable" + (n > 1 ? "s" : "") + " au plan d'adressage)");
            }
        } else if (link_) {
            for (auto& e : link_->takeEvents()) event(std::move(e));
            link_->stop();
            link_.reset();
            linkKey_.clear();
            event("Liaison Modbus TCP arr\xC3\xAAt\xC3\xA9" "e : l'IHM lit le simulateur");
        }
        // Le serveur de demonstration.
        if (comm.demoServer) {
            const std::string key = std::to_string(comm.demoPort) + (comm.demoAllInterfaces ? "*" : "") + (comm.wordOrder == "fort" ? "F" : "f") + "|"
                                    + plan->signature();
            if (!demo_ || !demo_->running() || key != demoKey_) {
                if (demo_) demo_->stop();
                bank_ = hmi::comm::makeSimBank(plc, comm, oracleOf(rt));
                if (rt) bank_->exchange(*rt);
                demo_ = std::make_unique<hmi::modbus::Server>();
                std::string why;
                const std::string bind = comm.demoAllInterfaces ? "0.0.0.0" : "127.0.0.1";
                if (demo_->start(bind, comm.demoPort, bank_, &why)) {
                    demoError_.clear();
                    event("Serveur de d\xC3\xA9monstration en marche : le simulateur en Modbus TCP sur " + bind + ":" + std::to_string(demo_->port()) + " ("
                          + std::to_string(bank_->plan().points().size()) + " variables)");
                } else {
                    demoError_ = why;
                    event("Serveur de d\xC3\xA9monstration : " + why);
                    demo_.reset();
                    bank_.reset();
                }
                demoKey_ = key;
            }
        } else if (demo_ || !demoKey_.empty()) {
            if (demo_) {
                demo_->stop();
                event("Serveur de d\xC3\xA9monstration arr\xC3\xAAt\xC3\xA9");
            }
            demo_.reset();
            bank_.reset();
            demoKey_.clear();
            demoError_.clear();
        }
    }
    if (bank_ && rt && demo_ && demo_->running()) {
        publishAcc_ += dt;
        // Le simulateur vient d'arriver (ou il a change) : tout de suite, sans
        // laisser la liaison lire les zeros d'avant.
        if (changed || publishAcc_ >= 0.05) {
            publishAcc_ = 0;
            bank_->exchange(*rt);
        }
    }
    if (link_)
        for (auto& e : link_->takeEvents()) event(std::move(e));
}

std::vector<CommHost::TestLine> CommHost::test(const hmi::Communication& comm, const domain::Project* plc, sim::Runtime* rt) {
    return testLink(planFor(comm, plc, rt), hmi::comm::settingsOf(comm));
}

std::vector<CommHost::TestLine> CommHost::testLink(const hmi::comm::Plan& plan, const hmi::comm::Settings& settings, std::string planLine,
                                                   const ShowValue& show) {
    using namespace hmi::comm;
    std::vector<TestLine> out;
    const auto t0 = std::chrono::steady_clock::now();
    const bool equipment = !planLine.empty();
    if (planLine.empty()) {
        std::size_t fromProgram = 0, fromTable = 0;
        for (const auto& p : plan.points()) (p.origin == "table" ? fromTable : fromProgram)++;
        planLine = "Plan d'adressage : " + std::to_string(plan.points().size()) + " variable(s) - " + std::to_string(fromProgram) + " du programme, "
                   + std::to_string(fromTable) + " de la table ; " + std::to_string(plan.refused().size()) + " sans place";
    }
    out.push_back({planLine, plan.points().empty() ? 2 : 0});
    hmi::modbus::Client client({settings.host, settings.port, settings.unit, settings.timeoutMs});
    const auto c = client.connect();
    const std::string where = settings.host + ":" + std::to_string(settings.port);
    if (!c.ok) {
        out.push_back({"Connexion \xC3\xA0 " + where + " : " + c.why, 3});
        if (c.why.find("refus") != std::string::npos)
            out.push_back({"Rien n'\xC3\xA9" "coute sur ce port : le serveur Modbus de l'automate est-il activ\xC3\xA9 ? le port est-il le bon (502) ?", 2});
        else if (c.why.find("inconnue") != std::string::npos)
            out.push_back({"Le nom ne se r\xC3\xA9sout pas : donnez l'adresse IP de l'automate.", 2});
        else
            out.push_back({"L'automate ne r\xC3\xA9pond pas : l'adresse IP, le c\xC3\xA2" "ble, le r\xC3\xA9seau (le m\xC3\xAAme sous-r\xC3\xA9seau), un pare-feu ?", 2});
        out.push_back({"Essai termin\xC3\xA9 en " + std::to_string(msSince(t0)) + " ms", 0});
        return out;
    }
    out.push_back({"Connexion \xC3\xA0 " + where + " : \xC3\xA9tablie en " + std::to_string(static_cast<long long>(std::lround(c.ms))) + " ms", 1});
    hmi::modbus::Identification id;
    const auto io = client.readIdentification(id);
    if (io.ok) {
        std::string text;
        for (const auto& [objectId, value] : id)
            if (!value.empty() && objectId != 3) text += (text.empty() ? "" : " \xC2\xB7 ") + value;
        out.push_back({"Identification : " + (text.empty() ? std::string("(vide)") : text), 1});
    } else if (io.exception) {
        out.push_back({"Identification : pas servie par l'\xC3\xA9quipement (exception " + hmi::modbus::exceptionText(io.exception) + ")", 0});
    } else {
        out.push_back({"Identification : " + io.why, 3});
        if (!client.connected()) return out;
    }
    // La table d'abord (ce que l'on a place soi-meme), puis le programme.
    std::vector<Point> pts;
    for (const bool table : {true, false})
        for (const auto& p : plan.points()) {
            if (pts.size() >= 60) break;
            if ((p.origin == "table") != table) continue;
            if (!p.array) {
                pts.push_back(p);
                continue;
            }
            // Un tableau : sa premiere case.
            if (auto e = plan.resolve(p.name + "[" + std::to_string(p.low) + "]")) pts.push_back(*e);
        }
    if (pts.empty()) {
        out.push_back({equipment ? std::string("Aucune variable \xC3\xA0 lire : liez des variables IHM \xC3\xA0 cet \xC3\xA9quipement (Lier une variable : onglet \xC3\x89quipements ou Plan d'adressage).")
                                 : std::string("Aucune variable \xC3\xA0 lire : localisez des variables du programme (AT %MW...) ou remplissez la table des adresses."),
                       2});
    } else {
        const auto blocks = planBlocks(pts, settings.maxWords, settings.maxBits, settings.gap);
        std::size_t ok = 0, requests = 0;
        std::vector<std::pair<std::size_t, std::string>> refused;
        std::vector<std::pair<std::size_t, sim::Value>> values;
        bool alive = true;
        const auto readOne = [&](std::size_t k) {
            const auto& p = pts[k];
            ++requests;
            hmi::modbus::Outcome o;
            if (p.bits()) {
                std::vector<bool> b;
                o = client.readBits(p.area == Area::DiscreteInputs, p.offset, 1, b);
                if (o.ok && !b.empty()) values.emplace_back(k, decodeBit(p, b.front()));
            } else {
                std::vector<std::uint16_t> w;
                o = client.readRegisters(p.area == Area::InputRegisters, p.offset, p.size, w);
                if (o.ok && w.size() >= p.size) values.emplace_back(k, decodeWords(p, w.data(), settings.lowWordFirst));
            }
            if (o.ok) ++ok;
            else if (o.exception) refused.emplace_back(k, "exception " + hmi::modbus::exceptionText(o.exception));
            else {
                refused.emplace_back(k, o.why);
                alive = false;
            }
        };
        for (const auto& b : blocks) {
            if (!alive) break;
            ++requests;
            const bool isBits = b.area == Area::Coils || b.area == Area::DiscreteInputs;
            std::vector<bool> bits;
            std::vector<std::uint16_t> words;
            const auto o = isBits ? client.readBits(b.area == Area::DiscreteInputs, b.start, b.count, bits)
                                  : client.readRegisters(b.area == Area::InputRegisters, b.start, b.count, words);
            if (o.ok) {
                for (const auto k : b.items) {
                    const auto& p = pts[k];
                    const std::size_t at = p.offset - b.start;
                    if (isBits && at < bits.size()) values.emplace_back(k, decodeBit(p, bits[at]));
                    else if (!isBits && at + p.size <= words.size()) values.emplace_back(k, decodeWords(p, words.data() + at, settings.lowWordFirst));
                    ++ok;
                }
            } else if (o.exception) {
                for (const auto k : b.items) {
                    if (!alive) break;
                    readOne(k);
                }
            } else {
                out.push_back({"Lecture : " + o.why, 3});
                alive = false;
            }
        }
        out.push_back({"Lecture : " + std::to_string(ok) + " variable(s) sur " + std::to_string(pts.size()) + " en " + std::to_string(requests) + " requ\xC3\xAAte(s)",
                       refused.empty() && alive ? 1 : 2});
        std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (std::size_t i = 0; i < values.size() && i < 6; ++i) {
            const auto& p = pts[values[i].first];
            out.push_back({"    " + p.name + " (" + p.address + ") = " + (show ? show(p, values[i].second) : hmi::formatValue(values[i].second)), 0});
        }
        if (values.size() > 6) out.push_back({"    ... et " + std::to_string(values.size() - 6) + " autre(s)", 0});
        for (std::size_t i = 0; i < refused.size() && i < 5; ++i) {
            const auto& p = pts[refused[i].first];
            out.push_back({"    " + p.name + " (" + p.address + ") : " + refused[i].second, 3});
        }
    }
    if (!plan.refused().empty()) {
        std::string names;
        for (std::size_t i = 0; i < plan.refused().size() && i < 3; ++i) names += (i ? ", " : "") + plan.refused()[i].first;
        out.push_back({std::to_string(plan.refused().size()) + " variable(s) sans place (" + names + (plan.refused().size() > 3 ? "..." : "")
                           + ") : G\xC3\xA9n\xC3\xA9rer dit celles que l'IHM utilise",
                       0});
    }
    client.disconnect();
    out.push_back({"Essai termin\xC3\xA9 en " + std::to_string(msSince(t0)) + " ms", 0});
    return out;
}

} // namespace app
