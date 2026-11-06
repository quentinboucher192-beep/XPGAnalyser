// =============================================================================
//  app/hmi/HmiDeployService.cpp - 1.28.0 : le volet Deploiement reseau, branche
//                                 sur deploy/ (voir le .hpp)
// =============================================================================
#include "HmiDeployService.hpp"

#include "HmiStationLaunch.hpp"
#include "../../deploy/Client.hpp"
#include "../../deploy/Crypto.hpp"
#include "../../deploy/DevKey.hpp"
#include "../../deploy/Discovery.hpp"
#include "../../deploy/Package.hpp"
#include "../../deploy/Pairing.hpp"
#include "../../deploy/TlsIdentity.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <system_error>

namespace app {

namespace {

using Machine = HmiDeployPane::Machine;
using Result = HmiDeployPane::Result;

constexpr int kConnectMs = 5000;

std::filesystem::path P(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }
std::string U(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string fingerprintText(const std::string& hexFp) {
    deploy::crypto::Bytes b;
    if (!deploy::crypto::unhex(hexFp, b, 32)) return hexFp;
    deploy::crypto::Digest d{};
    std::copy(b.begin(), b.end(), d.begin());
    return deploy::crypto::fingerprintText(d, 4);
}

std::string clock(std::int64_t ms) {
    const std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%d/%m %H:%M", &tm);
    return buf;
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string duration(std::int64_t s) {
    if (s < 60) return std::to_string(s) + " s";
    if (s < 3600) return std::to_string(s / 60) + " min";
    if (s < 86400) return std::to_string(s / 3600) + " h " + std::to_string((s % 3600) / 60) + " min";
    return std::to_string(s / 86400) + " j " + std::to_string((s % 86400) / 3600) + " h";
}

// Un nom de fichier sur : lettres, chiffres, - _ . (le reste devient _).
std::string fileSafe(std::string s) {
    for (auto& c : s) {
        const auto u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.') || u >= 0x80) c = '_';
    }
    return s.empty() ? std::string("projet") : s;
}

// L'etat partage des gestes (un seul fil de travail l'emploie a la fois ; identity() le lit depuis l'ecran).
struct State {
    std::string folder;
    std::function<std::string()> program;
    int discoveryPort{0};
    std::mutex mutex;   // pour identity() (l'ecran) et les caches
    bool loaded{false};
    std::string loadError;
    deploy::TlsIdentity me;
    std::unique_ptr<deploy::DevKey> key;
    std::unique_ptr<deploy::MachineStore> store;
    std::map<std::string, deploy::discovery::Found> found;   // par empreinte (hexadecimal)
    std::map<std::string, std::string> states;               // l'etat dit d'une machine (par empreinte)
    std::vector<std::pair<std::string, std::string>> identity;

    // Les cles de ce PC et le magasin (crees s'ils manquent). Faux : `why`.
    bool ensure(std::string* why) {
        if (loaded) return true;
        std::error_code ec;
        std::filesystem::create_directories(P(folder), ec);
        std::string w;
        if (!deploy::crypto::init()) {
            if (why) *why = "le hasard du syst\xC3\xA8me est indisponible (libsodium)";
            return false;
        }
        deploy::TlsIdentity id;
        if (!deploy::TlsIdentity::loadOrCreate(folder, "studio", false, "XPG-Studio " + deploy::DevKey::computerName(), id, &w)) {
            if (why) *why = "le certificat de ce PC : " + w;
            return false;
        }
        auto k = deploy::DevKey::loadOrCreate(folder, &w);
        if (!k) {
            if (why) *why = "la cl\xC3\xA9 d'\xC3\xA9" "diteur : " + w;
            return false;
        }
        auto s = std::make_unique<deploy::MachineStore>(folder);
        if (!s->load(&w)) {
            if (why) *why = "les machines appair\xC3\xA9" "es : " + w;
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        me = id;
        key = std::move(k);
        store = std::move(s);
        identity = {{"Nom du PC", deploy::DevKey::computerName()},
                    {"Empreinte de son certificat", deploy::crypto::fingerprintText(me.fingerprint(), 4)},
                    {"Ta cl\xC3\xA9 d'\xC3\xA9" "diteur", key->fingerprint()},
                    {"Dossier", folder}};
        loaded = true;
        return true;
    }

    std::vector<Machine> rows() {
        std::vector<Machine> out;
        const auto paired = store ? store->machines() : std::vector<deploy::PairedMachine>{};
        for (const auto& p : paired) {
            Machine m;
            m.name = p.name;
            m.address = p.address;
            m.port = p.port;
            m.id = p.id;
            m.fingerprint = fingerprintText(p.fingerprint);
            m.paired = true;
            m.seen = "appair\xC3\xA9" "e le " + clock(p.pairedAt);
            if (const auto it = found.find(p.fingerprint); it != found.end()) {
                m.version = it->second.version;
                m.address = it->second.address;     // l'adresse ou elle a repondu (DHCP)
                m.port = it->second.port;
            }
            const auto st = states.find(p.fingerprint);
            m.state = st != states.end() ? st->second : std::string("Appair\xC3\xA9" "e");
            out.push_back(std::move(m));
        }
        for (const auto& [fp, f] : found) {
            if (std::any_of(paired.begin(), paired.end(), [&](const deploy::PairedMachine& p) { return p.fingerprint == fp; })) continue;
            Machine m;
            m.name = f.machine;
            m.address = f.address;
            m.port = f.port;
            m.id = f.id;
            m.version = f.version;
            m.fingerprint = fingerprintText(fp);
            const auto st = states.find(fp);
            m.state = st != states.end() ? st->second : std::string("D\xC3\xA9" "couverte");
            m.seen = "r\xC3\xA9pond";
            out.push_back(std::move(m));
        }
        return out;
    }

    bool pairedOf(const Machine& m, deploy::PairedMachine& out) {
        if (!store) return false;
        for (const auto& p : store->machines())
            if (p.id == m.id || fingerprintText(p.fingerprint) == m.fingerprint) {
                out = p;
                out.address = m.address;   // la derniere adresse connue
                out.port = m.port;
                return true;
            }
        return false;
    }

    std::string foundFp(const Machine& m) {
        for (const auto& [fp, f] : found)
            if (f.id == m.id) return fp;
        return {};
    }

    deploy::Client client() { return deploy::Client(me, deploy::DevKey::computerName(), key->publicKeyHex()); }

    Result fail(std::string message) {
        Result r;
        r.error = true;
        r.message = std::move(message);
        r.hasMachines = static_cast<bool>(store);
        if (store) r.machines = rows();
        return r;
    }
    Result done(std::string message) {
        Result r;
        r.message = std::move(message);
        r.hasMachines = true;
        r.machines = rows();
        return r;
    }
};

} // namespace

HmiDeployPane::Backend makeDeployBackend(std::string folder, std::function<std::string()> program, int discoveryPort) {
    auto st = std::make_shared<State>();
    st->folder = std::move(folder);
    st->program = std::move(program);
    st->discoveryPort = discoveryPort;
    HmiDeployPane::Backend b;

    b.identity = [st] {
        std::lock_guard<std::mutex> lock(st->mutex);
        return st->identity;
    };

    b.machines = [st] {
        std::string why;
        if (!st->ensure(&why)) return st->fail("D\xC3\xA9ploiement r\xC3\xA9seau indisponible : " + why);
        Result r = st->done({});
        return r;
    };

    b.discover = [st](bool) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Rechercher : " + why);
        deploy::discovery::Options o;
        if (st->discoveryPort > 0) o.port = st->discoveryPort;
        for (const auto& p : st->store->machines()) o.direct.push_back(p.address);   // aussi en direct (un autre sous-reseau)
        const auto list = deploy::discovery::discover(o, &why);
        if (list.empty() && !why.empty()) return st->fail("Rechercher : " + why);
        for (const auto& f : list) {
            st->found[f.fingerprint] = f;
            st->states.erase(f.fingerprint);
        }
        const auto n = list.size();
        return st->done(n == 0 ? std::string("Rechercher : aucun agent n'a r\xC3\xA9pondu (lanc\xC3\xA9 sur la machine ? le port UDP 47862 "
                                             "ouvert ? sinon : Ajouter par adresse)")
                               : "Rechercher : " + std::to_string(n) + (n > 1 ? " machines ont r\xC3\xA9pondu" : " machine a r\xC3\xA9pondu"));
    };

    b.add = [st](const std::string& address, int port) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Ajouter : " + why);
        auto c = st->client();
        auto o = c.connect(address, port, kConnectMs);
        if (!o) return st->fail("Ajouter " + address + " : " + o.message);
        deploy::AgentInfo info;
        o = c.identify(info);
        c.close();
        if (!o) return st->fail("Ajouter " + address + " : " + o.message);
        deploy::discovery::Found f;
        f.address = address;
        f.port = port;
        f.machine = info.machine;
        f.id = info.id;
        f.version = info.version;
        f.fingerprint = info.fingerprint;
        f.protos = info.protos;
        st->found[f.fingerprint] = f;
        return st->done("Ajout\xC3\xA9" "e : " + info.machine + " (" + info.id + ", agent " + info.version + ")"
                        + (info.paired ? " - elle te conna\xC3\xAEt d\xC3\xA9j\xC3\xA0" : " - \xC3\xA0 appairer"));
    };

    b.pair = [st](const Machine& m, const std::string& code) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Appairer : " + why);
        auto c = st->client();
        auto o = c.connect(m.address, m.port, kConnectMs);
        if (!o) return st->fail("Appairer " + m.name + " : " + o.message);
        deploy::AgentInfo info;
        o = c.identify(info);
        if (!o) return st->fail("Appairer " + m.name + " : " + o.message);
        deploy::PairedMachine pm;
        o = c.pair(code, pm);
        c.close();
        if (!o) return st->fail("Appairer " + m.name + " : " + o.message);
        if (!st->store->upsert(pm, &why)) return st->fail("Appairer " + m.name + " : appair\xC3\xA9" "e, mais pas gard\xC3\xA9" "e ici (" + why + ")");
        st->states.erase(pm.fingerprint);
        return st->done("Appair\xC3\xA9" "e : " + pm.name + " (" + pm.id + ") - son empreinte " + fingerprintText(pm.fingerprint)
                        + " est gard\xC3\xA9" "e ici ; la tienne et ta cl\xC3\xA9 d'\xC3\xA9" "diteur l\xC3\xA0-bas");
    };

    b.status = [st](const Machine& m) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("\xC3\x89tat : " + why);
        deploy::PairedMachine pm;
        if (!st->pairedOf(m, pm)) return st->fail("\xC3\x89tat : " + m.name + " n'est pas appair\xC3\xA9" "e");
        auto c = st->client();
        auto o = c.connect(pm, kConnectMs);
        deploy::AgentStatus s;
        if (o) o = c.status(s);
        c.close();
        if (!o) {
            st->states[pm.fingerprint] = o.error == "empreinte" ? std::string("Empreinte chang\xC3\xA9" "e") : std::string("Injoignable");
            return st->fail("\xC3\x89tat de " + m.name + " : " + o.message);
        }
        st->states[pm.fingerprint] = "Appair\xC3\xA9" "e \xC2\xB7 en ligne";
        Result r = st->done("\xC3\x89tat de " + s.machine + " : agent " + s.version + ", en marche depuis " + duration(s.uptimeS));
        r.hasDetails = true;
        r.details = {{"Syst\xC3\xA8me", s.system},
                     {"Version de l'agent", s.version},
                     {"En marche depuis", duration(s.uptimeS)},
                     {"Place libre", s.diskFree < 0 ? std::string("inconnue") : deploy::packageSizeText(static_cast<std::uint64_t>(s.diskFree))},
                     {"PC appair\xC3\xA9s \xC3\xA0 l'agent", std::to_string(s.pairedCount)},
                     {"Applications install\xC3\xA9" "es", s.apps.empty() ? std::string("aucune (l'installation vient en 1.29.0)")
                                                                       : std::to_string(s.apps.size())},
                     {"Lu \xC3\xA0", clock(nowMs())}};
        return r;
    };

    b.logs = [st](const Machine& m) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Journal : " + why);
        deploy::PairedMachine pm;
        if (!st->pairedOf(m, pm)) return st->fail("Journal : " + m.name + " n'est pas appair\xC3\xA9" "e");
        auto c = st->client();
        auto o = c.connect(pm, kConnectMs);
        std::vector<std::string> lines;
        if (o) o = c.logs(200, lines);
        c.close();
        if (!o) return st->fail("Journal de " + m.name + " : " + o.message);
        Result r = st->done("Journal de " + m.name + " : " + std::to_string(lines.size()) + " ligne(s)");
        r.hasLogs = true;
        r.logs = std::move(lines);
        return r;
    };

    b.forget = [st](const Machine& m) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Oublier : " + why);
        deploy::PairedMachine pm;
        if (!st->pairedOf(m, pm)) {
            const auto fp = st->foundFp(m);
            if (!fp.empty()) st->found.erase(fp);
            return st->done("Oubli\xC3\xA9" "e : " + m.name + " (elle n'\xC3\xA9tait pas appair\xC3\xA9" "e)");
        }
        // D'abord lui demander de l'oublier (si elle repond), puis l'oublier ici quoi qu'il arrive.
        auto c = st->client();
        auto o = c.connect(pm, 3000);
        if (o) o = c.unpair();
        c.close();
        if (!st->store->remove(pm.fingerprint, &why)) return st->fail("Oublier " + m.name + " : " + why);
        st->states.erase(pm.fingerprint);
        if (o) return st->done("Oubli\xC3\xA9" "e : " + m.name + " - et elle t'a oubli\xC3\xA9 aussi");
        return st->done("Oubli\xC3\xA9" "e ici : " + m.name + " - elle n'a pas r\xC3\xA9pondu (" + o.message
                        + ") : elle garde ton empreinte, mais ce PC ne la croit plus");
    };

    b.createPackage = [st](const std::string& projectFolder) {
        std::string why;
        if (!st->ensure(&why)) return st->fail("Cr\xC3\xA9" "er un package : " + why);
        deploy::ProjectInfo info;
        if (!deploy::readProjectInfo(projectFolder, info, &why)) return st->fail("Cr\xC3\xA9" "er un package : " + why);
        deploy::PackageBuildOptions o;
        o.projectFolder = projectFolder;
        const std::string exe = st->program ? st->program() : std::string{};
        o.launcherName = station::launcherName(true);
        o.launcherText = station::launcherText(exe.empty() ? std::string("IHM-Studio.exe") : exe, info.name, true);
        const std::string version = info.lastVersion.empty() ? info.version : info.lastVersion;
        const auto out = P(projectFolder) / "exports" / P(fileSafe(info.name) + "-" + fileSafe(version) + ".xpgdeploy");
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        o.outputPath = U(out);
        const auto built = deploy::buildPackage(o, *st->key);
        if (!built.ok) return st->fail("Cr\xC3\xA9" "er un package : " + built.error);
        // Le relire comme le fera l'agent : signature, chemins, chaque empreinte.
        deploy::PackageVerifyOptions v;
        v.approvedKeys.push_back(st->key->publicKey());
        const auto report = deploy::verifyPackage(built.path, v);
        if (!report.ok()) return st->fail("Cr\xC3\xA9" "er un package : cr\xC3\xA9\xC3\xA9 mais refus\xC3\xA9 \xC3\xA0 la relecture : " + report.reason);
        Result r = st->done("Package cr\xC3\xA9\xC3\xA9 et v\xC3\xA9rifi\xC3\xA9 : " + built.path + " - "
                            + std::to_string(built.manifest.files.size()) + " fichiers, " + deploy::packageSizeText(built.packageBytes)
                            + ", sign\xC3\xA9 par ta cl\xC3\xA9 " + st->key->fingerprint());
        r.package = built.path;
        r.hasDetails = false;
        return r;
    };
    return b;
}

} // namespace app
