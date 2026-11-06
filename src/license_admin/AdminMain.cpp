// =============================================================================
//  license_admin/AdminMain.cpp - 1.30.0 (prepare en 1.29.0) : XPG-License-Admin,
//  l'outil de l'EDITEUR pour les licences, en console
// -----------------------------------------------------------------------------
//  JAMAIS LIVRE : cible a part (xpg_license_admin), construite sur le poste Admin
//  seulement ; ni dans l'installateur, ni dans un zip (outils/mingw/package_mingw.py
//  arrete la construction s'il trouve son nom ou sa marque dans le dossier a
//  livrer). Voir docs/LICENCES.md.
//
//    XPG-License-Admin creer-cle [--essai] [--nom N] [--dossier D]
//    XPG-License-Admin cle [--dossier D]
//    XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp [--dossier D]
//    XPG-License-Admin signer-registre --entree registre.json --sortie registre.xpgreg [--dossier D]
//    XPG-License-Admin signer-revocations --entree revocations.json --sortie revocations.xpgrev [--dossier D]
//    XPG-License-Admin signer-licence --parametres p.json --demande pc.xpgreq [--demande ...]
//                      [--licence ancienne.xpglic] --registre registre.xpgreg
//                      [--revocations revocations.xpgrev] --sortie licence.xpglic [--dossier D]
//    XPG-License-Admin verifier FICHIER [--dossier D]
//    XPG-License-Admin --version
//  Code de sortie : 0 fait ; 1 refuse (la raison est ecrite) ; 2 mauvais usage.
// =============================================================================
#include "Admin.hpp"

#include "../deploy/Crypto.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

namespace crypto = deploy::crypto;
using namespace license_admin;

constexpr const char* kToolVersion = "1.0.0";

void say(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void complain(const std::string& text) {
    const std::string line = text + "\n";
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

std::string usage() {
    return std::string("XPG-License-Admin ") + kToolVersion + " - l'outil de l'\xC3\xA9" "diteur pour les licences (" + kAdminMarker + ")\n"
           "Il ne se livre jamais : il reste sur le poste Admin.\n\n"
           "  creer-cle [--essai] [--nom N] [--dossier D]   cr\xC3\xA9" "e la cl\xC3\xA9 de signature de l'\xC3\xA9" "diteur (prot\xC3\xA9g\xC3\xA9" "e sur ce poste)\n"
           "  cle [--dossier D]                              montre la cl\xC3\xA9 (identifiant, empreinte, essai ou non)\n"
           "  exporter-cle --sortie FICHIER.hpp              \xC3\xA9" "crit la cl\xC3\xA9 PUBLIQUE en C++ (src/licensing/PublisherKey.hpp)\n"
           "  signer-registre --entree R.json --sortie R.xpgreg\n"
           "  signer-revocations --entree V.json --sortie V.xpgrev\n"
           "  signer-licence --parametres P.json --demande PC.xpgreq [--demande ...] [--licence ANCIENNE.xpglic]\n"
           "                 --registre R.xpgreg [--revocations V.xpgrev] --sortie L.xpglic\n"
           "  verifier FICHIER                               lit un fichier sign\xC3\xA9 et dit ce qu'il contient\n"
           "  --version\n\n"
           "--dossier : le dossier de la cl\xC3\xA9 (par d\xC3\xA9" "faut " + defaultKeyFolder() + ").\n"
           "Code de sortie : 0 fait, 1 refus\xC3\xA9, 2 mauvais usage.\n";
}

bool readText(const std::string& path, std::string& out, std::string& why) {
    crypto::Bytes raw;
    if (!crypto::readFile(path, raw, &why, 16u * 1024u * 1024u)) return false;
    out.assign(raw.begin(), raw.end());
    return true;
}

bool writeText(const std::string& path, const std::string& text, std::string& why) {
    crypto::Bytes bytes(text.begin(), text.end());
    return crypto::writePrivateFile(path, bytes, &why);
}

struct Args {
    std::string                                     command;
    std::map<std::string, std::vector<std::string>> options;   // --cle -> valeurs
    std::vector<std::string>                        positional;
    std::vector<std::string>                        flags;      // --essai
    [[nodiscard]] std::string one(const std::string& k) const {
        auto it = options.find(k);
        return it == options.end() || it->second.empty() ? std::string() : it->second.back();
    }
    [[nodiscard]] bool flag(const std::string& f) const {
        for (const auto& x : flags)
            if (x == f) return true;
        return false;
    }
};

int run(const std::vector<std::string>& argv) {
    if (argv.empty() || argv[0] == "aide" || argv[0] == "--aide" || argv[0] == "-h" || argv[0] == "--help") {
        say(usage());
        return argv.empty() ? 2 : 0;
    }
    if (argv[0] == "--version") {
        say(std::string("XPG-License-Admin ") + kToolVersion + " (" + kAdminMarker + ")\n");
        return 0;
    }
    Args a;
    a.command = argv[0];
    static const std::vector<std::string> kFlags = {"--essai"};
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& x = argv[i];
        if (x.rfind("--", 0) == 0) {
            if (std::find(kFlags.begin(), kFlags.end(), x) != kFlags.end()) {
                a.flags.push_back(x);
            } else if (i + 1 < argv.size()) {
                a.options[x].push_back(argv[++i]);
            } else {
                complain("l'option " + x + " attend une valeur");
                return 2;
            }
        } else {
            a.positional.push_back(x);
        }
    }
    const std::string folder = a.one("--dossier").empty() ? defaultKeyFolder() : a.one("--dossier");
    const licensing::Seconds now = licensing::systemNow();
    std::string why;
    if (!crypto::init()) {
        complain("libsodium ne d\xC3\xA9marre pas (le hasard du syst\xC3\xA8me est indisponible)");
        return 1;
    }
    const auto need = [&](const std::string& opt) -> bool {
        if (a.one(opt).empty()) {
            complain(a.command + " : l'option " + opt + " manque\n\n" + usage());
            return false;
        }
        return true;
    };
    const auto keyOrFail = [&]() -> std::unique_ptr<SigningKey> {
        auto key = loadPublisherKey(folder, &why);
        if (!key) complain("Refus\xC3\xA9 : " + why + "\n(cr\xC3\xA9" "er la cl\xC3\xA9 : XPG-License-Admin creer-cle --dossier " + folder + ")");
        return key;
    };
    const auto showKey = [&](const SigningKey& key) {
        say("Cl\xC3\xA9 de l'\xC3\xA9" "diteur : " + key.name() + "\n  identifiant " + key.keyId() + "\n  empreinte   " + key.fingerprint(0) + "\n  fichier     " + key.path()
            + "\n  protection  " + (crypto::protectedHere() ? "DPAPI (ce compte Windows)" : "fichier lisible par son seul propri\xC3\xA9taire (pas de DPAPI ici)") + "\n"
            + (key.test() ? "  !!! CL\xC3\x89 D'ESSAI : jamais pour des licences de clients ; un programme qui l'embarque n'est pas livrable !!!\n" : ""));
    };

    if (a.command == "creer-cle") {
        auto key = createPublisherKey(folder, a.one("--nom"), a.flag("--essai"), &why);
        if (!key) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cr\xC3\xA9\xC3\xA9" "e.\n");
        showKey(*key);
        say("Sauvegarde : garde une copie chiffr\xC3\xA9" "e hors ligne de cette cl\xC3\xA9 ; sans elle, plus aucune licence ne peut \xC3\xAAtre sign\xC3\xA9" "e.\n");
        return 0;
    }
    if (a.command == "cle") {
        auto key = keyOrFail();
        if (!key) return 1;
        showKey(*key);
        return 0;
    }
    if (a.command == "exporter-cle") {
        if (!need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        if (!writeText(a.one("--sortie"), publisherKeyHeader(*key, now), why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9 publique \xC3\xA9" "crite : " + a.one("--sortie") + " (" + key->keyId() + (key->test() ? ", CL\xC3\x89 D'ESSAI" : "") + ")\n");
        return 0;
    }
    if (a.command == "signer-registre" || a.command == "signer-revocations") {
        if (!need("--entree") || !need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        std::string input, out;
        if (!readText(a.one("--entree"), input, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        const bool ok = a.command == "signer-registre" ? signRegistry(*key, input, now, out, &why) : signRevocations(*key, input, now, out, &why);
        if (!ok || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string text;
        (void)describeFile(out, key.get(), text);
        say("Sign\xC3\xA9 : " + a.one("--sortie") + "\n" + text);
        return 0;
    }
    if (a.command == "signer-licence") {
        if (!need("--parametres") || !need("--registre") || !need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        LicenseInputs in;
        if (!readText(a.one("--parametres"), in.params, why) || !readText(a.one("--registre"), in.registry, why)
            || (!a.one("--revocations").empty() && !readText(a.one("--revocations"), in.revocations, why))
            || (!a.one("--licence").empty() && !readText(a.one("--licence"), in.existing, why))) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        auto it = a.options.find("--demande");
        if (it != a.options.end())
            for (const auto& path : it->second) {
                std::string req;
                if (!readText(path, req, why)) {
                    complain("Refus\xC3\xA9 : " + why);
                    return 1;
                }
                in.requests.push_back(std::move(req));
            }
        std::string out, summary;
        if (!signLicense(*key, in, now, out, &why, &summary) || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sign\xC3\xA9" "e : " + a.one("--sortie") + "\n" + summary + (key->test() ? "!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI !!!\n" : ""));
        return 0;
    }
    if (a.command == "verifier") {
        if (a.positional.empty()) {
            complain("verifier : quel fichier ?\n\n" + usage());
            return 2;
        }
        std::string bytes;
        if (!readText(a.positional[0], bytes, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string keyWhy;
        auto key = loadPublisherKey(folder, &keyWhy);
        std::string text;
        const bool ok = describeFile(bytes, key.get(), text);
        say(text);
        return ok ? 0 : 1;
    }
    complain("commande inconnue : " + a.command + "\n\n" + usage());
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    // Les arguments en UTF-8 (un dossier accentue passe), la console en UTF-8.
    (void)argc;
    (void)argv;
    SetConsoleOutputCP(CP_UTF8);
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; w && i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 1 ? len - 1 : 0), '\0');
        if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
        args.push_back(std::move(s));
    }
    if (w) LocalFree(w);
#else
    args.assign(argv + 1, argv + argc);
#endif
    return run(args);
}
