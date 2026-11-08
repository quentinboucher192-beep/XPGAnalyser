// =============================================================================
//  tests/admin_test_fixture.hpp - 1.33.0 : un POSTE ADMIN D'ESSAI pour les essais
//  de l'application de l'editeur (Workspace et pages)
// -----------------------------------------------------------------------------
//  Un dossier temporaire : une cle d'ESSAI (jamais une vraie), le registre
//  d'exemple signe (tests/fixtures/licences/registre-exemple.json), et des
//  demandes d'activation de PC fictifs (des LicenseStore dans des dossiers
//  voisins : "Poste essai A", "Poste essai B"...). Aucun client reel, aucune
//  donnee de client : des noms d'essai seulement.
// =============================================================================
#pragma once

#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/Store.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace admintest {

namespace fs = std::filesystem;
namespace lic = licensing;
namespace gui = license_admin::gui;

inline int g_checks = 0, g_failures = 0;
inline void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
inline bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
inline std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
inline std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
// 1.35.0 : un texte (un secret) se trouve-t-il dans un fichier de ce dossier (tout l'arbre) ?
inline bool foundIn(const fs::path& dir, const std::string& needle) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && readFile(it->path()).find(needle) != std::string::npos) return true;
    }
    return false;
}
// 1.37.0 : ... ailleurs que dans ce sous-dossier (le code garde, codes-activation/ : chiffre sous Windows).
inline bool foundOutside(const fs::path& dir, const std::string& needle, const std::string& keptIn) {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->path().parent_path().filename() == keptIn) continue;
        if (it->is_regular_file(ec) && readFile(it->path()).find(needle) != std::string::npos) return true;
    }
    return false;
}
inline int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

// L'heure fixe des essais (avancee a la main).
inline lic::Seconds g_now = 1793961600;   // 2026-11-06T12:00:00Z

struct Fixture {
    fs::path    root;            // le dossier temporaire
    fs::path    admin;           // le dossier du poste Admin (la cle, l'archive, activations.jsonl)
    std::string sourceRoot;      // la racine du depot (argv[1])
    std::unique_ptr<gui::Workspace> ws;

    explicit Fixture(const std::string& source, const std::string& tag) : sourceRoot(source) {
        std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
        root = fs::temp_directory_path() / ("xpg-admin-" + tag + "-" + std::to_string(rng() % 1000000000ULL));
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root, ec);
        admin = root / "admin";
        ws = std::make_unique<gui::Workspace>(U(admin));
        ws->setClock([] { return g_now; });
        ws->reload();
    }
    ~Fixture() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    // La cle d'ESSAI et le registre d'exemple signe et range (le poste pret a signer des licences).
    bool ready() {
        if (!ws->key().present && !ws->createKey("essai application", true).ok) return false;
        if (!ws->hasRegistry()) {
            const auto r = ws->signRegistryJson(sourceRoot + "/tests/fixtures/licences/registre-exemple.json");
            if (!r.ok) {
                std::printf("  (registre : %s)\n", r.text().c_str());
                return false;
            }
        }
        return ws->key().present && ws->hasRegistry();
    }

    // Les cles de confiance d'un PC client d'essai : celle du poste Admin d'essai.
    [[nodiscard]] lic::StoreOptions pcOptions(const fs::path& folder) const {
        lic::StoreOptions o;
        o.folder = U(folder);
        o.compiledKeys = false;
        if (const auto* k = ws->signingKey()) {
            lic::TrustedKey t;
            t.id = k->keyId();
            t.name = k->name();
            t.key = k->publicKey();
            t.test = k->test();
            o.trustedKeys = {t};
        }
        o.clock = [] { return g_now; };
        o.machineId = "machine-essai-" + folder.filename().string();
        return o;
    }

    // Une demande d'activation d'un PC fictif (son dossier : root/<pc>) ; rend le chemin du .xpgreq.
    std::string request(const std::string& pc, const std::string& deviceName, const std::string& licenseId = {}) {
        const fs::path folder = root / pc;
        std::error_code ec;
        fs::create_directories(folder, ec);
        lic::LicenseStore store(pcOptions(folder));
        const fs::path out = root / (pc + ".xpgreq");
        std::string why;
        if (!store.writeActivationRequest(U(out), deviceName, licenseId, "IHM Studio 1.33.0 (essai)", &why)) std::printf("  (demande : %s)\n", why.c_str());
        return U(out);
    }

    // Signer une licence d'essai pour un client d'essai ; rend le resultat (le .xpglic dans root/sortie).
    gui::OpResult licence(const std::string& id, const std::string& customer, const std::string& edition, const std::vector<std::string>& requests,
                          int durationDays = -1) {
        gui::LicenceDraft d;
        d.licenseId = id;
        d.customer = customer;
        d.edition = edition;
        d.requestPaths = requests;
        d.durationDays = durationDays;
        std::error_code ec;
        fs::create_directories(root / "sortie", ec);
        return ws->signLicence(d, U(root / "sortie" / (id + ".xpglic")));
    }
};

} // namespace admintest
