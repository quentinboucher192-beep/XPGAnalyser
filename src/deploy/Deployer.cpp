// =============================================================================
//  deploy/Deployer.cpp - 1.29.0 : l'envoi, l'installation, le poste (voir Deployer.hpp)
// =============================================================================
#include "Deployer.hpp"

#include "Crypto.hpp"
#include "DevKey.hpp"
#include "Install.hpp"
#include "Paths.hpp"
#include "Protocol.hpp"
#include "Station.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace deploy {

namespace {

namespace fs = std::filesystem;
namespace err = protocol::err;
using core::json::Value;

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string cite(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }

// "1,2 s", "45 s".
std::string seconds(std::int64_t ms) {
    if (ms >= 10000) return std::to_string((ms + 500) / 1000) + " s";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld,%lld s", static_cast<long long>(ms / 1000), static_cast<long long>((ms % 1000) / 100));
    return buf;
}

bool isHex64(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

OpResult failed(std::string_view code, std::string message, Value data = Value::object()) {
    OpResult r;
    r.ok = false;
    r.code = std::string(code);
    r.message = std::move(message);
    r.data = std::move(data);
    r.journal = r.code + " : " + r.message;
    return r;
}

OpResult succeeded(Value data, std::string journal) {
    OpResult r;
    r.ok = true;
    r.data = std::move(data);
    r.journal = std::move(journal);
    return r;
}

// Un entier d'un argument, borne.
std::int64_t clampArg(const Value& args, std::string_view key, std::int64_t fallback, std::int64_t lo, std::int64_t hi) {
    if (!args.at(key).isNumber()) return fallback;
    return std::clamp<std::int64_t>(args.integer(key, fallback), lo, hi);
}

// Le lanceur, reecrit pour le programme de CE PC (comme app/hmi/HmiStationLaunch::launcherText).
std::string launcherText(const std::string& exe, const std::string& app, const std::string& label, bool windows) {
    std::string t;
    if (windows) {
        t += "@echo off\r\n";
        t += "rem Poste d'exploitation - " + app + " " + label + " (IHM Studio)\r\n";
        t += "rem Installe par XPG-Deployment-Agent, pour le programme de ce PC. Le projet est le dossier de ce fichier.\r\n";
        t += "chcp 65001 >nul\r\n";
        t += "start \"\" \"" + exe + "\" --ihm \"%~dp0.\"\r\n";
    } else {
        std::string q = "\"";
        for (const char c : exe) {
            if (c == '"' || c == '$' || c == '`' || c == '\\') q += '\\';
            q += c;
        }
        q += "\"";
        t += "#!/bin/sh\n";
        t += "# Poste d'exploitation - " + app + " " + label + " (IHM Studio)\n";
        t += "# Installe par XPG-Deployment-Agent, pour le programme de ce PC. Le projet est le dossier de ce fichier.\n";
        t += "cd \"$(dirname \"$0\")\" || exit 1\n";
        t += "exec " + q + " --ihm \"$(pwd)\" \"$@\"\n";
    }
    return t;
}

} // namespace

std::string defaultRuntimeProgram(const std::string& agentExe) {
#if defined(_WIN32)
    const char* name = "IHM-Studio.exe";
#else
    const char* name = "IHM-Studio";
#endif
    if (agentExe.empty()) return name;
    return U((P(agentExe).parent_path() / name).make_preferred());
}

struct Deployer::Impl {
    DeployerConfig cfg;
    pairing::StudioStore& studios;
    Journal& journal;
    const std::atomic<bool>& stopping;
    install::AppStore store;
    install::History hist;
    std::string program;

    std::mutex transferMutex;   // incoming/
    std::mutex opsMutex;        // INSTALL, STOP, START, ROLLBACK : une a la fois
    std::mutex stationMutex;    // `station`

    struct Running {
        station::Process proc;
        std::string app, id, folder, stopFile;
        std::int64_t startedAt{0};
        bool adopted{false};
    };
    std::unique_ptr<Running> station;
    int lastExitCode{-1};

    Impl(DeployerConfig c, pairing::StudioStore& s, Journal& j, const std::atomic<bool>& st)
        : cfg(std::move(c)), studios(s), journal(j), stopping(st), store(cfg.dir), hist(cfg.dir) {
        program = cfg.runtimeProgram.empty() ? defaultRuntimeProgram(station::currentExecutable()) : cfg.runtimeProgram;
    }

    std::string incoming(const std::string& sha, const char* ext) const { return joinPath(joinPath(cfg.dir, "incoming"), sha + ext); }
    std::string postePath() const { return joinPath(cfg.dir, "poste.json"); }
    std::string stopPath() const { return joinPath(cfg.dir, "arret.json"); }
    std::string resumePath() const { return joinPath(cfg.dir, "reprise-poste.txt"); }

    std::vector<crypto::PublicKey> approvedKeys() const {
        std::vector<crypto::PublicKey> keys;
        for (const auto& s : studios.all()) {
            crypto::Bytes b;
            if (!crypto::unhex(s.publisherKey, b, 32)) continue;
            crypto::PublicKey k{};
            std::copy(b.begin(), b.end(), k.begin());
            keys.push_back(k);
        }
        return keys;
    }

    std::int64_t freeBytes() const {
        if (cfg.simulatedFreeBytes >= 0) return cfg.simulatedFreeBytes;
        std::error_code ec;
        const auto sp = fs::space(P(cfg.dir), ec);
        return ec ? -1 : static_cast<std::int64_t>(sp.available);
    }

    void record(std::string_view op, const Peer& peer, const std::string& app, const std::string& id, const std::string& label, bool ok,
                const std::string& result, const std::string& message) {
        Value e = Value::object();
        e.set("at", protocol::nowMs());
        e.set("op", std::string(op));
        e.set("application", app);
        e.set("version", id);
        e.set("label", label);
        e.set("from", peer.name);
        e.set("studio", peer.fingerprint.substr(0, 16));
        e.set("address", peer.address);
        e.set("ok", ok);
        e.set("result", result);
        e.set("message", message);
        hist.append(e);
    }

    // L'application d'une demande : nommee, sinon la seule installee.
    bool pickApp(const Value& args, install::AppInfo& out, OpResult& error) {
        std::string name = args.str("application");
        if (name.empty()) {
            const auto all = store.list();
            if (all.size() == 1) {
                out = all.front();
                return true;
            }
            error = failed(err::kBadArgs, all.empty() ? "aucune application n'est install\xC3\xA9" "e sur ce PC"
                                                      : "plusieurs applications sont install\xC3\xA9" "es sur ce PC : dis laquelle");
            return false;
        }
        if (!install::validName(name, nullptr) || !store.load(name, out)) {
            error = failed(err::kUnknownApp, "l'application " + cite(name) + " n'est pas install\xC3\xA9" "e sur ce PC");
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------- le poste --
    void persistLocked() {
        std::error_code ec;
        if (!station) {
            fs::remove(P(postePath()), ec);
            return;
        }
        Value v = Value::object();
        v.set("format", "xpg-agent-poste");
        v.set("pid", station->proc.pid());
        // En texte : l'heure de creation Windows (FILETIME, ~1,3e17) ne tient pas dans un nombre JSON (double).
        v.set("created", std::to_string(station->proc.created()));
        v.set("application", station->app);
        v.set("version", station->id);
        v.set("folder", station->folder);
        v.set("stopFile", station->stopFile);
        v.set("startedAt", station->startedAt);
        v.set("adopted", station->adopted);
        (void)install::atomicWrite(postePath(), core::json::write(v, true) + "\n", nullptr);
    }

    // Le poste tourne-t-il ? Un poste sorti est oublie ; sans poste connu, on cherche celui que la session a lance.
    bool refreshLocked() {
        if (station && !station->proc.running()) {
            lastExitCode = station->proc.exitCode();
            std::error_code ec;
            fs::remove(P(station->stopFile), ec);
            journal.write("", "POSTE", "sorti : " + station->app + " " + station->id + " pid " + std::to_string(station->proc.pid())
                                           + (lastExitCode >= 0 ? " code " + std::to_string(lastExitCode) : std::string()));
            station.reset();
            persistLocked();
        }
        if (!station) {
            const auto found = station::findStations(program, store.root());
            if (!found.empty()) adoptLocked(found.front());
        }
        return station != nullptr;
    }

    void adoptLocked(const station::Found& f) {
        auto r = std::make_unique<Running>();
        if (!r->proc.attach(f.pid, f.created)) return;
        // <racine>/<application>/versions/<id>
        const fs::path folder = P(f.folder).lexically_normal();
        r->id = U(folder.filename());
        r->app = U(folder.parent_path().parent_path().filename());
        r->folder = f.folder;
        r->stopFile = stopPath();
        r->startedAt = protocol::nowMs();
        r->adopted = true;
        journal.write("", "POSTE", "retrouv\xC3\xA9 (lanc\xC3\xA9 par la session) : " + r->app + " " + r->id + " pid " + std::to_string(f.pid));
        station = std::move(r);
        persistLocked();
    }

    Value viewLocked() {
        Value v = Value::object();
        const bool on = refreshLocked();
        v.set("running", on);
        v.set("pid", on ? station->proc.pid() : 0);
        v.set("application", on ? station->app : std::string());
        v.set("version", on ? station->id : std::string());
        v.set("startedAt", on ? station->startedAt : std::int64_t(0));
        v.set("adopted", on && station->adopted);
        return v;
    }

    // Attendre la sortie du poste `ms` au plus. Vrai : sorti.
    bool waitGone(std::int64_t ms, int* code) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(stationMutex);
                if (!station || !station->proc.running()) {
                    if (station) {
                        lastExitCode = station->proc.exitCode();
                        std::error_code ec;
                        fs::remove(P(station->stopFile), ec);
                        station.reset();
                        persistLocked();
                    }
                    if (code) *code = lastExitCode;
                    return true;
                }
            }
            if (stopping.load() || std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // Lancer la version `id` de `app`. Faux : `code` et `why`.
    bool launch(const std::string& app, const std::string& id, std::string& code, std::string& why, int& pid) {
        std::error_code ec;
        if (!fs::is_regular_file(P(program), ec)) {
            code = std::string(err::kNoRuntime);
            why = "le programme du poste est introuvable : " + program + " (IHM Studio doit \xC3\xAAtre install\xC3\xA9 \xC3\xA0 c\xC3\xB4t\xC3\xA9 de l'agent)";
            return false;
        }
        auto r = std::make_unique<Running>();
        r->app = app;
        r->id = id;
        r->folder = store.versionDir(app, id);
        r->stopFile = stopPath();
        fs::remove(P(r->stopFile), ec);   // une vieille demande d'arret ne doit pas l'arreter aussitot
        // L'etat ecrit par le poste a l'arret est repris par le suivant ; trop vieux (10 minutes), il s'efface.
        const auto age = fs::file_time_type::clock::now() - fs::last_write_time(P(resumePath()), ec);
        if (!ec && age > std::chrono::minutes(10)) fs::remove(P(resumePath()), ec);
        ec.clear();
        if (!r->proc.launch(program, {"--ihm", r->folder}, {{"XPG_ARRET_FICHIER", r->stopFile}, {"XPG_REPRISE_FICHIER", resumePath()}},
                            r->folder, joinPath(cfg.dir, "poste-sortie.log"), &why)) {
            code = std::string(err::kInternal);
            return false;
        }
        r->startedAt = protocol::nowMs();
        pid = r->proc.pid();
        std::lock_guard<std::mutex> lock(stationMutex);
        station = std::move(r);
        persistLocked();
        journal.write("", "POSTE", "lanc\xC3\xA9 : " + app + " " + id + " pid " + std::to_string(pid) + " (" + program + " --ihm)");
        return true;
    }

    // Le poste tient-il `ms` ? Faux : il est sorti (`ranMs`, `exitCode`).
    bool holds(std::int64_t ms, std::int64_t& ranMs, int& exitCode) {
        const auto t0 = std::chrono::steady_clock::now();
        const bool gone = waitGone(ms, &exitCode);
        ranMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return !gone;
    }

    // Demander l'arret du poste en marche ; `result` dit ce qui s'est passe.
    OpResult stopRunning(const Peer& peer, const std::string& reason, std::int64_t delay, bool force) {
        Value d = Value::object();
        std::string app, id, stopFile;
        int pid = 0;
        bool adopted = false;
        {
            std::lock_guard<std::mutex> lock(stationMutex);
            if (!refreshLocked()) {
                d.set("wasRunning", false);
                return succeeded(d, "aucun poste en marche");
            }
            app = station->app;
            id = station->id;
            stopFile = station->stopFile;
            pid = station->proc.pid();
            adopted = station->adopted;
        }
        d.set("wasRunning", true);
        d.set("pid", pid);
        d.set("application", app);
        d.set("version", id);
        d.set("adopted", adopted);
        Value req = Value::object();
        req.set("raison", reason);
        req.set("delai", delay);
        std::string w;
        if (!install::atomicWrite(stopFile, core::json::write(req, false) + "\n", &w))
            return failed(err::kInternal, "impossible d'\xC3\xA9" "crire la demande d'arr\xC3\xAAt : " + w, d);
        journal.write("", "POSTE", "arr\xC3\xAAt demand\xC3\xA9 : " + app + " " + id + " pid " + std::to_string(pid) + ", d\xC3\xA9lai " + std::to_string(delay)
                                       + " s (" + reason + ")");
        const std::int64_t wait = delay * 1000 + cfg.stopGraceMs;
        const auto t0 = std::chrono::steady_clock::now();
        int code = -1;
        const bool gone = waitGone(wait, &code);
        const std::int64_t waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        d.set("waitedMs", waited);
        if (gone) {
            d.set("stopped", true);
            d.set("forced", false);
            d.set("exitCode", code);
            record("STOP", peer, app, id, "", true, "arrete", "le poste a quitt\xC3\xA9 en " + seconds(waited));
            return succeeded(d, app + " " + id + " pid " + std::to_string(pid) + " a quitt\xC3\xA9 en " + seconds(waited));
        }
        if (stopping.load()) return failed(err::kInternal, "l'agent s'arr\xC3\xAAte : l'arr\xC3\xAAt du poste est abandonn\xC3\xA9", d);
        const std::string late = "le poste (" + app + " " + id + ", pid " + std::to_string(pid) + ") n'a pas quitt\xC3\xA9 " + seconds(waited)
                                 + " apr\xC3\xA8s la demande d'arr\xC3\xAAt";
        if (!force) {
            d.set("stopped", false);
            record("STOP", peer, app, id, "", false, "delai", late);
            return failed(err::kStopTimeout,
                          late + " : il tourne toujours. Relance l'arr\xC3\xAAt avec \xC2\xAB forcer \xC2\xBB pour le terminer (ce que l'op\xC3\xA9rateur n'a pas enregistr\xC3\xA9 sera perdu).",
                          d);
        }
        bool killed = false;
        {
            std::lock_guard<std::mutex> lock(stationMutex);
            if (station) killed = station->proc.terminate(&w);
            else killed = true;
        }
        int c2 = -1;
        (void)waitGone(2000, &c2);
        journal.write("", "POSTE", "termin\xC3\xA9 de force : " + app + " " + id + " pid " + std::to_string(pid) + " (" + late + ")");
        d.set("stopped", killed);
        d.set("forced", true);
        if (!killed) {
            record("STOP", peer, app, id, "", false, "echec", late + " ; le terminer a \xC3\xA9" "chou\xC3\xA9 : " + w);
            return failed(err::kInternal, late + " ; le terminer a \xC3\xA9" "chou\xC3\xA9 : " + w, d);
        }
        record("STOP", peer, app, id, "", true, "force", late + " : termin\xC3\xA9 de force");
        return succeeded(d, app + " " + id + " pid " + std::to_string(pid) + " termin\xC3\xA9 de force (" + late + ")");
    }

    // La cle Run / le .desktop du poste suit la version active (si l'application demarre avec la session).
    std::string applyAutostart(const std::string& app, const std::string& id, bool on, bool explicitOff) {
        std::string where, w;
        if (on) {
            if (!station::setStationAutostart(true, program, store.versionDir(app, id), store.appDir(app), &where, &w))
                return "d\xC3\xA9marrage avec la session : " + w;
            journal.write("", "SESSION", "le poste d\xC3\xA9marre avec la session : " + app + " " + id + " (" + where + ")");
        } else if (explicitOff) {
            if (!station::setStationAutostart(false, program, {}, store.appDir(app), &where, &w)) return "d\xC3\xA9marrage avec la session : " + w;
            journal.write("", "SESSION", "le poste ne d\xC3\xA9marre plus avec la session : " + app);
        }
        return {};
    }

    // Activer `id` (active.json d'un coup), la cle Run suit.
    bool activate(install::AppInfo& a, const std::string& id, bool autostart, bool explicitOff, std::string& why, std::string& note) {
        const std::string before = a.active;
        if (!store.setActive(a.name, id, before == id ? a.previous : before, autostart, &why)) return false;
        if (before != id) a.previous = before;
        a.active = id;
        a.autostart = autostart;
        note = applyAutostart(a.name, id, autostart, explicitOff);
        return true;
    }

    // Lancer `target`, verifier qu'il tient ; sinon revenir a `fallback` et le relancer.
    OpResult launchChecked(const Peer& peer, install::AppInfo& a, const std::string& target, const std::string& fallback, std::int64_t checkMs,
                           bool allowRollback) {
        Value d = Value::object();
        d.set("application", a.name);
        d.set("version", target);
        d.set("checkMs", checkMs);
        // L'etat ecrit par le poste arrete : une copie, que le retour automatique remet si la
        // nouvelle version l'a pris (puis efface) avant de tomber.
        const std::string keep = resumePath() + ".avant-lancement";
        std::error_code ec;
        fs::remove(P(keep), ec);
        if (fs::exists(P(resumePath()), ec)) fs::copy_file(P(resumePath()), P(keep), fs::copy_options::overwrite_existing, ec);
        std::string code, why;
        int pid = 0;
        if (!launch(a.name, target, code, why, pid)) {
            record("START", peer, a.name, target, "", false, "echec", why);
            return failed(code, why, d);
        }
        d.set("pid", pid);
        std::int64_t ran = 0;
        int exitCode = -1;
        if (holds(checkMs, ran, exitCode)) {
            fs::remove(P(keep), ec);
            d.set("running", true);
            d.set("rolledBack", false);
            record("START", peer, a.name, target, "", true, "lance", "en marche (pid " + std::to_string(pid) + ")");
            return succeeded(d, a.name + " " + target + " pid " + std::to_string(pid) + " tient " + seconds(checkMs));
        }
        const std::string died = a.name + " " + target + " s'est arr\xC3\xAAt\xC3\xA9 au bout de " + seconds(ran)
                                 + (exitCode >= 0 ? " (code " + std::to_string(exitCode) + ")" : std::string());
        d.set("running", false);
        d.set("exitCode", exitCode);
        d.set("ranMs", ran);
        d.set("failedVersion", target);
        journal.write("", "POSTE", died);
        if (!allowRollback || fallback.empty() || fallback == target || !a.find(fallback)) {
            d.set("rolledBack", false);
            record("START", peer, a.name, target, "", false, "echec", died);
            return failed(err::kStartFailed, died + (allowRollback ? " ; aucune version pr\xC3\xA9" "c\xC3\xA9" "dente pour revenir en arri\xC3\xA8re." : "."), d);
        }
        // Le retour automatique a la version precedente.
        std::string note;
        if (!activate(a, fallback, a.autostart, false, why, note)) {
            d.set("rolledBack", false);
            record("START", peer, a.name, target, "", false, "echec", died + " ; retour impossible : " + why);
            return failed(err::kStartFailed, died + " ; le retour \xC3\xA0 " + fallback + " a \xC3\xA9" "chou\xC3\xA9 : " + why, d);
        }
        d.set("rolledBack", true);
        d.set("rolledBackTo", fallback);
        if (!fs::exists(P(resumePath()), ec) && fs::exists(P(keep), ec)) fs::rename(P(keep), P(resumePath()), ec);
        fs::remove(P(keep), ec);
        journal.write("", "ROLLBACK", "automatique : " + a.name + " " + target + " -> " + fallback + " (" + died + ")");
        std::string msg = died + " : retour automatique \xC3\xA0 " + fallback;
        int pid2 = 0;
        if (!launch(a.name, fallback, code, why, pid2)) {
            d.set("running", false);
            msg += ", qui n'a pas pu \xC3\xAAtre relanc\xC3\xA9" "e (" + why + ").";
        } else {
            std::int64_t ran2 = 0;
            int code2 = -1;
            const bool ok2 = holds(checkMs, ran2, code2);
            d.set("pid", pid2);
            d.set("running", ok2);
            msg += ok2 ? ", relanc\xC3\xA9" "e et en marche (pid " + std::to_string(pid2) + ")."
                       : ", relanc\xC3\xA9" "e, mais elle s'est aussi arr\xC3\xAAt\xC3\xA9" "e au bout de " + seconds(ran2) + ".";
        }
        record("START", peer, a.name, target, "", false, "retour", msg);
        record("ROLLBACK", peer, a.name, fallback, "", true, "automatique", msg);
        return failed(err::kStartFailed, msg, d);
    }
};

Deployer::Deployer(DeployerConfig cfg, pairing::StudioStore& studios, Journal& journal, const std::atomic<bool>& stopping)
    : d_(std::make_unique<Impl>(std::move(cfg), studios, journal, stopping)) {}

Deployer::~Deployer() = default;

std::string Deployer::runtimeProgram() const { return d_->program; }

void Deployer::start() {
    auto& d = *d_;
    std::error_code ec;
    // Les installations coupees : les dossiers .extraction, les versions sans fiche.
    for (fs::directory_iterator a(P(d.store.root()), ec), end; !ec && a != end; a.increment(ec)) {
        std::error_code e2;
        for (fs::directory_iterator v(a->path() / "versions", e2), vend; !e2 && v != vend; v.increment(e2)) {
            std::error_code e3;
            if (!v->is_directory(e3)) continue;
            const fs::path p = v->path();
            fs::path fiche = p;
            fiche += u8".json";
            if (p.extension() == ".extraction" || !fs::exists(fiche, e3)) {
                fs::remove_all(p, e3);
                d.journal.write("", "NETTOYAGE", "installation coup\xC3\xA9" "e effac\xC3\xA9" "e : " + U(p));
            }
        }
    }
    // Le poste lance avant le redemarrage de l'agent.
    Value v;
    std::lock_guard<std::mutex> lock(d.stationMutex);
    if (install::readJson(d.postePath(), v, nullptr)) {
        auto r = std::make_unique<Impl::Running>();
        std::int64_t created = 0;
        try {
            created = v.at("created").isString() ? std::stoll(v.str("created")) : v.integer("created");
        } catch (...) {
            created = 0;
        }
        if (r->proc.attach(static_cast<int>(v.integer("pid")), created)) {
            r->app = v.str("application");
            r->id = v.str("version");
            r->folder = v.str("folder");
            r->stopFile = v.str("stopFile");
            r->startedAt = v.integer("startedAt");
            r->adopted = v.flag("adopted");
            d.journal.write("", "POSTE", "retrouv\xC3\xA9 : " + r->app + " " + r->id + " pid " + std::to_string(r->proc.pid()));
            d.station = std::move(r);
        } else {
            fs::remove(P(d.postePath()), ec);
        }
    }
    (void)d.refreshLocked();
}

// ================================================================ PREPARE ===
OpResult Deployer::prepare(const Peer& peer, const Value& args) {
    auto& d = *d_;
    const std::string app = args.str("application"), label = args.str("version"), sha = args.str("sha256");
    std::string w;
    if (!install::validName(app, &w)) return failed(err::kBadArgs, "nom d'application refus\xC3\xA9 " + cite(app) + " : " + w);
    if (!install::validName(label, &w)) return failed(err::kBadArgs, "nom de version refus\xC3\xA9 " + cite(label) + " : " + w);
    if (!isHex64(sha)) return failed(err::kBadArgs, "\"sha256\" : 64 chiffres hexad\xC3\xA9" "cimaux en minuscules");
    const std::int64_t size = args.integer("size", -1), content = args.integer("contentSize", 0);
    const auto maxSize = static_cast<std::int64_t>(d.cfg.limits.maxTotalBytes + 64ull * 1024 * 1024);
    if (size <= 0 || size > maxSize) return failed(err::kBadArgs, "\"size\" : la taille du paquet (1 octet \xC3\xA0 " + packageSizeText(static_cast<std::uint64_t>(maxSize)) + ")");
    if (content < 0) return failed(err::kBadArgs, "\"contentSize\" n\xC3\xA9gatif");
    Value data = Value::object();
    data.set("transfer", sha);
    data.set("chunkSize", static_cast<std::int64_t>(protocol::kChunkSize));
    // Deja installee, identique : rien a envoyer.
    install::AppInfo a;
    if (d.store.load(app, a)) {
        for (const auto& v : a.versions)
            if (v.sha256 == sha) {
                data.set("offset", size);
                data.set("installed", true);
                data.set("version", v.id);
                data.set("diskFree", d.freeBytes());
                return succeeded(data, app + " " + label + " : d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e (" + v.id + "), rien \xC3\xA0 envoyer");
            }
    }
    std::lock_guard<std::mutex> lock(d.transferMutex);
    const std::string part = d.incoming(sha, ".part"), metaPath = d.incoming(sha, ".json");
    std::error_code ec;
    std::int64_t offset = 0;
    Value meta;
    if (install::readJson(metaPath, meta, nullptr) && meta.integer("size") == size && meta.str("sha256") == sha) {
        const auto have = static_cast<std::int64_t>(fs::file_size(P(part), ec));
        if (!ec && have <= size) {
            // Le dernier bloc complet : un bloc ecrit a moitie (l'agent coupe) est retire.
            offset = have == size ? size : (have / protocol::kChunkSize) * protocol::kChunkSize;
            if (offset != have) fs::resize_file(P(part), static_cast<std::uintmax_t>(offset), ec);
            if (ec) offset = 0;
        }
    }
    if (offset == 0) {
        fs::create_directories(P(joinPath(d.cfg.dir, "incoming")), ec);
        std::ofstream(P(part), std::ios::binary | std::ios::trunc).close();
        meta = Value::object();
    }
    // La place : le reste du paquet, son contenu extrait, la marge.
    const std::int64_t freeNow = d.freeBytes();
    const std::int64_t need = (size - offset) + content + d.cfg.diskReserveBytes;
    data.set("diskFree", freeNow);
    if (freeNow >= 0 && freeNow < need) {
        Value det = data;
        det.set("needed", need);
        return failed(err::kNoSpace,
                      "pas assez de place sur le disque de ce PC : il faut " + packageSizeText(static_cast<std::uint64_t>(need)) + " (le paquet, son contenu install\xC3\xA9 et une marge de "
                          + packageSizeText(static_cast<std::uint64_t>(d.cfg.diskReserveBytes)) + "), il reste " + packageSizeText(static_cast<std::uint64_t>(freeNow))
                          + ". Lib\xC3\xA8re de la place, ou supprime d'anciennes versions.",
                      det);
    }
    meta.set("format", "xpg-agent-reception");
    meta.set("application", app);
    meta.set("version", label);
    meta.set("size", size);
    meta.set("sha256", sha);
    meta.set("contentSize", content);
    meta.set("from", peer.name);
    meta.set("studio", peer.fingerprint);
    if (!meta.has("startedAt")) meta.set("startedAt", protocol::nowMs());
    meta.set("verified", false);
    if (!install::atomicWrite(metaPath, core::json::write(meta, true) + "\n", &w)) return failed(err::kInstallFailed, w);
    // Les receptions abandonnees depuis plus de 7 jours s'effacent.
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 7);
    for (fs::directory_iterator it(P(joinPath(d.cfg.dir, "incoming")), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->path().stem() != P(sha) && it->last_write_time(e2) < old) fs::remove(it->path(), e2);
    }
    data.set("offset", offset);
    data.set("installed", false);
    return succeeded(data, app + " " + label + " " + packageSizeText(static_cast<std::uint64_t>(size)) + (offset ? ", reprise \xC3\xA0 l'octet " + std::to_string(offset) : std::string()));
}

// ============================================================= SEND_CHUNK ===
OpResult Deployer::sendChunk(const Peer&, const Value& args, std::string_view bytes) {
    auto& d = *d_;
    const std::string sha = args.str("transfer"), chunkSha = args.str("sha256");
    const std::int64_t offset = args.integer("offset", -1);
    if (!isHex64(sha) || !isHex64(chunkSha) || offset < 0) return failed(err::kBadArgs, "SEND_CHUNK : \"chunk\" doit porter transfer, offset et sha256");
    if (bytes.empty() || bytes.size() > protocol::kChunkSize)
        return failed(err::kBadArgs, "un bloc fait de 1 octet \xC3\xA0 1 Mio (" + std::to_string(bytes.size()) + " octets re\xC3\xA7us)");
    if (args.at("size").isNumber() && args.integer("size") != static_cast<std::int64_t>(bytes.size()))
        return failed(err::kBadChunk, "le bloc \xC3\xA0 l'octet " + std::to_string(offset) + " est incomplet");
    if (crypto::sha256Hex(bytes) != chunkSha)
        return failed(err::kBadChunk, "le bloc \xC3\xA0 l'octet " + std::to_string(offset) + " est ab\xC3\xAEm\xC3\xA9 (son SHA-256 ne correspond pas) : il n'est pas \xC3\xA9" "crit, renvoie-le");
    std::lock_guard<std::mutex> lock(d.transferMutex);
    Value meta;
    if (!install::readJson(d.incoming(sha, ".json"), meta, nullptr))
        return failed(err::kTransfer, "envoi inconnu de l'agent : recommence par PREPARE");
    const std::int64_t size = meta.integer("size");
    std::error_code ec;
    const auto have = static_cast<std::int64_t>(fs::file_size(P(d.incoming(sha, ".part")), ec));
    if (ec) return failed(err::kTransfer, "envoi inconnu de l'agent : recommence par PREPARE");
    Value data = Value::object();
    if (offset + static_cast<std::int64_t>(bytes.size()) <= have) {   // deja recu (la reponse s'etait perdue)
        data.set("received", have);
        data.set("complete", have == size);
        return succeeded(data, "bloc " + std::to_string(offset) + " d\xC3\xA9j\xC3\xA0 re\xC3\xA7u");
    }
    if (offset != have) {
        data.set("expected", have);
        return failed(err::kTransfer, "bloc hors d'ordre : l'agent attend l'octet " + std::to_string(have) + ", re\xC3\xA7u " + std::to_string(offset), data);
    }
    if (offset + static_cast<std::int64_t>(bytes.size()) > size)
        return failed(err::kTransfer, "le bloc d\xC3\xA9passe la taille annonc\xC3\xA9" "e du paquet (" + std::to_string(size) + " octets)");
    {
        std::ofstream out(P(d.incoming(sha, ".part")), std::ios::binary | std::ios::app);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            fs::resize_file(P(d.incoming(sha, ".part")), static_cast<std::uintmax_t>(have), ec);
            return failed(err::kNoSpace, "\xC3\xA9" "criture impossible dans incoming (disque plein ?)");
        }
    }
    const std::int64_t received = offset + static_cast<std::int64_t>(bytes.size());
    data.set("received", received);
    data.set("complete", received == size);
    return succeeded(data, "bloc " + std::to_string(offset));
}

// ========================================================= VERIFY_PACKAGE ===
OpResult Deployer::verify(const Peer& peer, const Value& args) {
    auto& d = *d_;
    const std::string sha = args.str("transfer");
    if (!isHex64(sha)) return failed(err::kBadArgs, "\"transfer\" : le SHA-256 du paquet (PREPARE)");
    std::lock_guard<std::mutex> lock(d.transferMutex);
    Value meta;
    if (!install::readJson(d.incoming(sha, ".json"), meta, nullptr)) return failed(err::kTransfer, "envoi inconnu de l'agent : recommence par PREPARE");
    const std::string part = d.incoming(sha, ".part");
    const std::string app = meta.str("application"), label = meta.str("version");
    std::string got, w;
    std::uint64_t size = 0;
    if (!crypto::sha256File(part, got, &size, &w)) return failed(err::kTransfer, "le paquet re\xC3\xA7u ne se lit pas : " + w);
    if (static_cast<std::int64_t>(size) != meta.integer("size"))
        return failed(err::kTransfer, "le paquet n'est pas complet : " + std::to_string(size) + " octets sur " + std::to_string(meta.integer("size")));
    const auto drop = [&] {
        std::error_code ec;
        fs::remove(P(part), ec);
        fs::remove(P(d.incoming(sha, ".json")), ec);
    };
    if (got != sha) {
        drop();
        const std::string m = "le paquet re\xC3\xA7u ne correspond pas \xC3\xA0 celui annonc\xC3\xA9 (SHA-256) : il est effac\xC3\xA9, renvoie-le.";
        d.record("VERIFY_PACKAGE", peer, app, "", label, false, "abime", m);
        return failed(err::kBadPackage, m);
    }
    PackageVerifyOptions vo;
    vo.approvedKeys = d.approvedKeys();
    vo.limits = d.cfg.limits;
    const PackageReport rep = verifyPackage(part, vo);
    if (!rep.ok()) {
        drop();
        d.record("VERIFY_PACKAGE", peer, app, "", label, false, "refuse", rep.reason);
        Value det = Value::object();
        det.set("step", rep.failedStep);
        return failed(err::kPackageRefused, rep.reason, det);
    }
    if (rep.manifest.application != app || rep.manifest.appVersion != label) {
        drop();
        const std::string m = "le paquet contient " + rep.manifest.application + " " + rep.manifest.appVersion + ", pas " + app + " " + label + " comme annonc\xC3\xA9 : refus\xC3\xA9.";
        d.record("VERIFY_PACKAGE", peer, app, "", label, false, "refuse", m);
        return failed(err::kPackageRefused, m);
    }
    meta.set("verified", true);
    meta.set("publisher", rep.publisherFingerprint);
    meta.set("launcher", rep.manifest.launcher);
    (void)install::atomicWrite(d.incoming(sha, ".json"), core::json::write(meta, true) + "\n", nullptr);
    Value data = Value::object();
    data.set("application", app);
    data.set("version", label);
    data.set("files", static_cast<std::uint64_t>(rep.manifest.files.size()));
    data.set("bytes", rep.manifest.totalSize);
    data.set("publisher", rep.publisherFingerprint);
    data.set("reason", rep.reason);
    return succeeded(data, rep.reason);
}

// ================================================================ INSTALL ===
OpResult Deployer::install(const Peer& peer, const Value& args) {
    auto& d = *d_;
    const std::string sha = args.str("transfer");
    if (!isHex64(sha)) return failed(err::kBadArgs, "\"transfer\" : le SHA-256 du paquet (PREPARE)");
    std::unique_lock<std::mutex> ops(d.opsMutex, std::try_to_lock);
    if (!ops.owns_lock()) return failed(err::kBusy, "une autre op\xC3\xA9ration (installation, arr\xC3\xAAt, lancement) est en cours sur ce PC : r\xC3\xA9" "essaie dans un instant");
    std::lock_guard<std::mutex> lock(d.transferMutex);
    Value meta;
    const bool hasMeta = install::readJson(d.incoming(sha, ".json"), meta, nullptr);
    std::string app = hasMeta ? meta.str("application") : args.str("application");
    if (!install::validName(app, nullptr)) return failed(err::kTransfer, "envoi inconnu de l'agent : recommence par PREPARE");
    install::AppInfo a;
    const bool known = d.store.load(app, a);
    if (!known) a.name = app;
    const bool explicitAutostart = args.at("autostart").isBool();
    const bool autostart = explicitAutostart ? args.flag("autostart") : a.autostart;
    Value data = Value::object();
    data.set("application", app);
    std::string id, label, w;
    bool already = false;
    for (const auto& v : a.versions)
        if (v.sha256 == sha) {
            id = v.id;
            label = v.label;
            already = true;
        }
    if (!already) {
        if (!hasMeta || !meta.flag("verified"))
            return failed(err::kTransfer, "le paquet n'a pas \xC3\xA9t\xC3\xA9 v\xC3\xA9rifi\xC3\xA9 (VERIFY_PACKAGE) : rien n'est install\xC3\xA9");
        label = meta.str("version");
        id = d.store.freeId(app, label);
        if (id.empty()) return failed(err::kInstallFailed, "trop de versions nomm\xC3\xA9" "es " + label + " sur ce PC");
        std::error_code ec;
        fs::create_directories(P(d.store.versionsDir(app)), ec);
        PackageExtractOptions xo;
        xo.approvedKeys = d.approvedKeys();
        xo.limits = d.cfg.limits;
        const std::string dest = d.store.versionDir(app, id);
        const PackageExtractResult x = extractPackage(d.incoming(sha, ".part"), dest, xo);
        if (!x.ok) {
            d.record("INSTALL", peer, app, id, label, false, "echec", x.error);
            return failed(x.report.ok() ? err::kInstallFailed : err::kPackageRefused, x.error);
        }
        // Le lanceur, pour le programme de CE PC.
        const std::string launcher = x.report.manifest.launcher.substr(kPackageAppPrefix.size());
        {
            const bool windows = launcher.size() >= 4 && launcher.compare(launcher.size() - 4, 4, ".cmd") == 0;
            std::ofstream out(P(joinPath(dest, launcher)), std::ios::binary | std::ios::trunc);
            out << launcherText(d.program, app, label, windows);
        }
        install::VersionInfo v;
        v.id = id;
        v.label = label;
        v.sha256 = sha;
        v.installedAt = protocol::nowMs();
        v.from = peer.name;
        v.publisher = x.report.publisherFingerprint;
        v.launcher = launcher;
        v.files = x.files;
        v.bytes = x.bytes;
        if (!d.store.writeVersion(app, v, &w)) {
            fs::remove_all(P(dest), ec);
            d.record("INSTALL", peer, app, id, label, false, "echec", w);
            return failed(err::kInstallFailed, w);
        }
        fs::remove(P(d.incoming(sha, ".part")), ec);
        fs::remove(P(d.incoming(sha, ".json")), ec);
        (void)d.store.load(app, a);
        data.set("files", static_cast<std::uint64_t>(x.files));
        data.set("bytes", x.bytes);
    } else {
        std::error_code ec;
        fs::remove(P(d.incoming(sha, ".part")), ec);
        fs::remove(P(d.incoming(sha, ".json")), ec);
    }
    const std::string previous = a.active;
    bool activated = false;
    std::string note;
    if (previous != id || autostart != a.autostart || explicitAutostart) {
        if (!d.activate(a, id, autostart, explicitAutostart && !autostart, w, note)) {
            d.record("INSTALL", peer, app, id, label, false, "echec", w);
            return failed(err::kInstallFailed, "install\xC3\xA9" "e, mais active.json n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + w);
        }
        activated = previous != id;
    }
    // Jamais la version d'un poste en marche (meme si ce n'est plus l'active).
    std::vector<std::string> protect{id, a.previous};
    {
        std::lock_guard<std::mutex> guard(d.stationMutex);
        if (d.refreshLocked() && d.station->app == app) protect.push_back(d.station->id);
    }
    const auto removed = d.store.prune(app, d.cfg.keepVersions, protect);
    Value rm = Value::array();
    for (const auto& r : removed) rm.push(r);
    data.set("version", id);
    data.set("label", label);
    data.set("previous", previous == id ? a.previous : previous);
    data.set("already", already);
    data.set("activated", activated);
    data.set("autostart", autostart);
    data.set("autostartNote", note);
    data.set("removed", rm);
    data.set("folder", d.store.versionDir(app, id));
    const std::string what = already ? (activated ? "d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e, r\xC3\xA9" "activ\xC3\xA9" "e" : "d\xC3\xA9j\xC3\xA0 install\xC3\xA9" "e et active : rien \xC3\xA0 refaire")
                                     : "install\xC3\xA9" "e et active";
    std::string msg = app + " " + id + " " + what + (previous.empty() || previous == id ? std::string() : " (avant : " + previous + ")");
    if (!removed.empty()) {
        std::string list;
        for (const auto& r : removed) list += (list.empty() ? "" : ", ") + r;
        msg += " ; versions effac\xC3\xA9" "es : " + list;
    }
    if (!note.empty()) msg += " ; " + note;
    data.set("message", msg);
    d.record("INSTALL", peer, app, id, label, true, already ? "deja-installee" : "installee", msg);
    return succeeded(data, msg);
}

// =================================================================== STOP ===
OpResult Deployer::stop(const Peer& peer, const Value& args) {
    auto& d = *d_;
    std::unique_lock<std::mutex> ops(d.opsMutex, std::try_to_lock);
    if (!ops.owns_lock()) return failed(err::kBusy, "une autre op\xC3\xA9ration est en cours sur ce PC : r\xC3\xA9" "essaie dans un instant");
    std::string reason = args.str("raison");
    if (reason.empty()) reason = "Mise \xC3\xA0 jour demand\xC3\xA9" "e depuis " + (peer.name.empty() ? std::string("IHM Studio") : peer.name);
    const std::int64_t delay = clampArg(args, "delai", 60, 0, 3600);
    return d.stopRunning(peer, reason.substr(0, 500), delay, args.flag("force"));
}

// ================================================================== START ===
OpResult Deployer::startStation(const Peer& peer, const Value& args) {
    auto& d = *d_;
    std::unique_lock<std::mutex> ops(d.opsMutex, std::try_to_lock);
    if (!ops.owns_lock()) return failed(err::kBusy, "une autre op\xC3\xA9ration est en cours sur ce PC : r\xC3\xA9" "essaie dans un instant");
    install::AppInfo a;
    OpResult e;
    if (!d.pickApp(args, a, e)) return e;
    const std::string target = args.str("version").empty() ? a.active : args.str("version");
    if (target.empty() || !a.find(target))
        return failed(err::kNoVersion, target.empty() ? "aucune version active de " + a.name + " sur ce PC" : "la version " + target + " de " + a.name + " n'est pas install\xC3\xA9" "e");
    {
        std::lock_guard<std::mutex> lock(d.stationMutex);
        if (d.refreshLocked()) {
            Value data = Value::object();
            data.set("pid", d.station->proc.pid());
            data.set("application", d.station->app);
            data.set("version", d.station->id);
            if (d.station->app == a.name && d.station->id == target) {
                data.set("alreadyRunning", true);
                data.set("running", true);
                return succeeded(data, a.name + " " + target + " tourne d\xC3\xA9j\xC3\xA0 (pid " + std::to_string(d.station->proc.pid()) + ")");
            }
            return failed(err::kBusy, "un poste tourne d\xC3\xA9j\xC3\xA0 (" + d.station->app + " " + d.station->id + ", pid " + std::to_string(d.station->proc.pid())
                                          + ") : arr\xC3\xAAte-le d'abord (STOP)", data);
        }
    }
    const bool explicitAutostart = args.at("autostart").isBool();
    const bool autostart = explicitAutostart ? args.flag("autostart") : a.autostart;
    std::string fallback = a.previous, w, note;
    if (target != a.active || explicitAutostart) {
        if (target != a.active) fallback = a.active;
        if (!d.activate(a, target, autostart, explicitAutostart && !autostart, w, note)) return failed(err::kInstallFailed, w);
    }
    const std::int64_t check = args.at("check").isNumber() ? clampArg(args, "check", 8, 1, 300) * 1000 : d.cfg.startCheckMs;
    OpResult r = d.launchChecked(peer, a, target, fallback, check, true);
    if (!note.empty()) r.data.set("autostartNote", note);
    return r;
}

// =============================================================== ROLLBACK ===
OpResult Deployer::rollback(const Peer& peer, const Value& args) {
    auto& d = *d_;
    std::unique_lock<std::mutex> ops(d.opsMutex, std::try_to_lock);
    if (!ops.owns_lock()) return failed(err::kBusy, "une autre op\xC3\xA9ration est en cours sur ce PC : r\xC3\xA9" "essaie dans un instant");
    install::AppInfo a;
    OpResult e;
    if (!d.pickApp(args, a, e)) return e;
    const std::string from = a.active;
    const std::string to = args.str("version").empty() ? d.store.olderThanActive(a) : args.str("version");
    if (to.empty() || !a.find(to)) {
        std::string list;
        for (const auto& v : a.versions) list += (list.empty() ? "" : ", ") + v.id;
        const std::string m = to.empty() ? "aucune version pr\xC3\xA9" "c\xC3\xA9" "dente de " + a.name + " sur ce PC (install\xC3\xA9" "es : " + list + ")"
                                         : "la version " + to + " de " + a.name + " n'est pas install\xC3\xA9" "e (install\xC3\xA9" "es : " + list + ")";
        d.record("ROLLBACK", peer, a.name, to, "", false, "echec", m);
        return failed(err::kNoVersion, m);
    }
    Value data = Value::object();
    data.set("application", a.name);
    data.set("from", from);
    data.set("to", to);
    bool wasRunning = false;
    {
        std::lock_guard<std::mutex> lock(d.stationMutex);
        wasRunning = d.refreshLocked() && d.station->app == a.name;
    }
    data.set("wasRunning", wasRunning);
    if (wasRunning) {
        std::string reason = args.str("raison");
        if (reason.empty()) reason = "Retour \xC3\xA0 la version " + to + " demand\xC3\xA9 depuis " + (peer.name.empty() ? std::string("IHM Studio") : peer.name);
        OpResult s = d.stopRunning(peer, reason, clampArg(args, "delai", 60, 0, 3600), args.flag("force"));
        if (!s.ok) {
            s.message = "retour en arri\xC3\xA8re annul\xC3\xA9 : " + s.message;
            d.record("ROLLBACK", peer, a.name, to, "", false, "echec", s.message);
            return s;
        }
        data.set("stop", s.data);
    }
    std::string w, note;
    if (!d.activate(a, to, a.autostart, false, w, note)) {
        d.record("ROLLBACK", peer, a.name, to, "", false, "echec", w);
        return failed(err::kInstallFailed, w, data);
    }
    std::string msg = a.name + " : retour de " + (from.empty() ? std::string("?") : from) + " \xC3\xA0 " + to;
    const bool restart = args.at("start").isBool() ? args.flag("start") : wasRunning;
    data.set("restarted", false);
    data.set("running", false);
    if (restart) {
        const std::int64_t check = args.at("check").isNumber() ? clampArg(args, "check", 8, 1, 300) * 1000 : d.cfg.startCheckMs;
        OpResult s = d.launchChecked(peer, a, to, {}, check, false);
        data.set("restarted", true);
        data.set("running", s.ok);
        data.set("pid", s.data.integer("pid"));
        if (!s.ok) {
            msg += " ; mais " + s.message;
            d.record("ROLLBACK", peer, a.name, to, "", false, "echec", msg);
            return failed(s.code, msg, data);
        }
        msg += ", relanc\xC3\xA9" "e (pid " + std::to_string(s.data.integer("pid")) + ")";
    }
    if (!note.empty()) msg += " ; " + note;
    data.set("message", msg);
    d.record("ROLLBACK", peer, a.name, to, "", true, "ok", msg);
    return succeeded(data, msg);
}

// ======================================================= LIST_APPS, HISTORY ===
OpResult Deployer::listApps(const Peer&, const Value&) {
    auto& d = *d_;
    Value station;
    {
        std::lock_guard<std::mutex> lock(d.stationMutex);
        station = d.viewLocked();
    }
    Value apps = Value::array();
    for (const auto& a : d.store.list()) {
        Value o = Value::object();
        o.set("name", a.name);
        o.set("active", a.active);
        const auto* act = a.find(a.active);
        o.set("activeLabel", act ? act->label : std::string());
        o.set("previous", a.previous);
        o.set("autostart", a.autostart);
        o.set("activatedAt", a.activatedAt);
        const bool mine = station.flag("running") && station.str("application") == a.name;
        o.set("running", mine);
        o.set("pid", mine ? station.integer("pid") : std::int64_t(0));
        o.set("runningVersion", mine ? station.str("version") : std::string());
        Value vs = Value::array();
        for (const auto& v : a.versions) {
            Value x = Value::object();
            x.set("id", v.id);
            x.set("label", v.label);
            x.set("sha256", v.sha256);
            x.set("installedAt", v.installedAt);
            x.set("from", v.from);
            x.set("publisher", v.publisher);
            x.set("files", v.files);
            x.set("bytes", v.bytes);
            x.set("active", v.id == a.active);
            vs.push(std::move(x));
        }
        o.set("versions", std::move(vs));
        apps.push(std::move(o));
    }
    Value data = Value::object();
    data.set("apps", std::move(apps));
    data.set("station", std::move(station));
    data.set("program", d.program);
    return succeeded(data, std::to_string(data.at("apps").items().size()) + " application(s)");
}

OpResult Deployer::history(const Peer&, const Value& args) {
    auto& d = *d_;
    const auto n = static_cast<std::size_t>(clampArg(args, "limit", 100, 1, 1000));
    Value list = Value::array();
    for (auto& e : d.hist.tail(n)) list.push(std::move(e));
    Value data = Value::object();
    data.set("entries", std::move(list));
    return succeeded(data, std::to_string(data.at("entries").items().size()) + " entr\xC3\xA9" "e(s)");
}

Value Deployer::appsSummary() {
    auto& d = *d_;
    Value station;
    {
        std::lock_guard<std::mutex> lock(d.stationMutex);
        station = d.viewLocked();
    }
    Value apps = Value::array();
    for (const auto& a : d.store.list()) {
        Value o = Value::object();
        o.set("name", a.name);
        o.set("version", a.active);
        const bool mine = station.flag("running") && station.str("application") == a.name;
        o.set("running", mine);
        o.set("pid", mine ? station.integer("pid") : std::int64_t(0));
        o.set("versions", static_cast<std::int64_t>(a.versions.size()));
        apps.push(std::move(o));
    }
    return apps;
}

Value Deployer::stationState() {
    std::lock_guard<std::mutex> lock(d_->stationMutex);
    return d_->viewLocked();
}

} // namespace deploy
