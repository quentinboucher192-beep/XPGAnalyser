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
//    XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp [--garder] [--dossier D]
//                      (une cle d'essai : --sortie src/licensing/TestPublisherKey.hpp)
//    XPG-License-Admin exporter-cle --sortie src/licensing/PublisherKey.hpp --vide
//    XPG-License-Admin signer-registre --entree registre.json --sortie registre.xpgreg [--dossier D]
//    XPG-License-Admin signer-revocations --entree revocations.json --sortie revocations.xpgrev [--dossier D]
//    XPG-License-Admin signer-licence --parametres p.json --demande pc.xpgreq [--demande ...]
//                      [--licence ancienne.xpglic] --registre registre.xpgreg
//                      [--revocations revocations.xpgrev] --sortie licence.xpglic [--dossier D]
//                      1.33.0 : [--retirer-pc CLE] (avec --licence : une nouvelle emission sans ce PC)
//    XPG-License-Admin verifier FICHIER [--dossier D]
//    XPG-License-Admin enregistrer-demande FICHIER.xpgreq [--dossier D]       (1.30.0)
//    XPG-License-Admin liste [--licence ID] [--client TEXTE] [--csv F.csv] [--dossier D]   (1.30.0)
//  1.31.0 : LA SYNCHRO AVEC UN DEPOT GITHUB PRIVE (GitHubSync.hpp) et l'ARCHIVE du poste (chaque fichier signe
//  y est range ; signer-* --publier : synchronise juste apres) :
//    XPG-License-Admin archiver FICHIER... [--dossier D]
//    XPG-License-Admin github-config --depot PROPRIETAIRE/NOM [--branche main] [--racine DOSSIER] [--code-secours CODE [--remplacer]]
//    XPG-License-Admin github-jeton [--variable NOM] | github-jeton --effacer
//    XPG-License-Admin github-etat [--hors-ligne]
//    XPG-License-Admin github-synchroniser [--recevoir-seulement]
//    XPG-License-Admin github-code-secours --afficher
//  1.32.0 : LA SAUVEGARDE DE LA CLE PAR UNE PHRASE DE PASSE (KeyBackup.hpp) - relisible sur un autre PC :
//    XPG-License-Admin sauvegarder-cle --sortie F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin verifier-sauvegarde F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin restaurer-cle --entree F.xpgcle [--phrase-variable NOM] [--dossier D]
//  La phrase se tape sans echo (deux fois pour sauvegarder) ; --phrase-variable la lit dans une variable
//  d'environnement (les essais, un script). Elle n'est jamais affichee ni ecrite.
//  1.30.0 : le registre des activations (activations.jsonl, dans le dossier de la cle) : signer-licence,
//  signer-revocations et enregistrer-demande y ajoutent leurs lignes ; liste montre tous les PC connus.
//    XPG-License-Admin --version
//  Code de sortie : 0 fait ; 1 refuse (la raison est ecrite) ; 2 mauvais usage ; 3 signe et range, mais la synchro
//  (--publier) a echoue (1.31.0 : ne pas signer de nouveau, relancer github-synchroniser).
// =============================================================================
#include "Admin.hpp"
#include "GitHubSync.hpp"
#include "KeyBackup.hpp"

#include "../core/SystemOpen.hpp"
#include "../deploy/Crypto.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace {

namespace crypto = deploy::crypto;
using namespace license_admin;

constexpr const char* kToolVersion = "1.1.0";   // 1.31.0 : la synchro GitHub, l'archive

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
           "  sauvegarder-cle --sortie F.xpgcle              une sauvegarde de la cl\xC3\xA9 chiffr\xC3\xA9" "e par une phrase de passe (tap\xC3\xA9" "e deux fois,\n"
           "                                                 sans \xC3\xA9" "cho) : elle se relit sur un autre PC, contrairement au fichier DPAPI\n"
           "  verifier-sauvegarde F.xpgcle                   v\xC3\xA9rifie la phrase et la sauvegarde, sans rien \xC3\xA9" "crire\n"
           "  restaurer-cle --entree F.xpgcle                remet la cl\xC3\xA9 sur ce PC (prot\xC3\xA9g\xC3\xA9" "e pour ce compte) ; une cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 reste\n"
           "  creer-cle [--essai] [--nom N] [--dossier D]   cr\xC3\xA9" "e la cl\xC3\xA9 de signature de l'\xC3\xA9" "diteur (prot\xC3\xA9g\xC3\xA9" "e sur ce poste)\n"
           "  cle [--dossier D]                              montre la cl\xC3\xA9 (identifiant, empreinte, essai ou non)\n"
           "  exporter-cle --sortie FICHIER.hpp [--garder]   \xC3\xA9" "crit la cl\xC3\xA9 PUBLIQUE en C++ : src/licensing/PublisherKey.hpp\n"
           "                                                 (une cl\xC3\xA9 d'essai : src/licensing/TestPublisherKey.hpp) ;\n"
           "                                                 --garder : les cl\xC3\xA9s d\xC3\xA9j\xC3\xA0 dans le fichier restent (une rotation)\n"
           "  exporter-cle --sortie PublisherKey.hpp --vide   le fichier SANS cl\xC3\xA9 (les licences ne sont pas actives)\n"
           "  signer-registre --entree R.json --sortie R.xpgreg\n"
           "  signer-revocations --entree V.json --sortie V.xpgrev\n"
           "                                                 V.json : number, revoked[] ; chaque entr\xC3\xA9" "e : license_id, reason,\n"
           "                                                 [device] (un seul PC), [issued_before] (les \xC3\xA9missions sign\xC3\xA9" "es avant cette date)\n"
           "  signer-licence --parametres P.json --demande PC.xpgreq [--demande ...] [--licence ANCIENNE.xpglic]\n"
           "                 --registre R.xpgreg [--revocations V.xpgrev] --sortie L.xpglic\n"
           "                 [--retirer-pc CLE_DU_PC ...] (avec --licence : une nouvelle \xC3\xA9mission sans ce PC)\n"
           "  verifier FICHIER                               lit un fichier sign\xC3\xA9 et dit ce qu'il contient\n"
           "  enregistrer-demande PC.xpgreq                  note une demande re\xC3\xA7ue (pas encore sign\xC3\xA9" "e) dans le registre des activations\n"
           "  liste [--licence ID] [--client TEXTE] [--csv F.csv]\n"
           "                                                 tous les PC connus (un par ligne, leur dernier \xC3\xA9tat), les totaux ;\n"
           "                                                 --csv : le m\xC3\xAAme tableau pour Excel (UTF-8, s\xC3\xA9parateur ;)\n"
           "\nL'archive et la synchro GitHub (un d\xC3\xA9p\xC3\xB4t PRIV\xC3\x89 ; les licences et le registre des activations y sont chiffr\xC3\xA9s) :\n"
           "  archiver FICHIER...                            range des fichiers sign\xC3\xA9s (registre, r\xC3\xA9vocations, licences) dans l'archive ;\n"
           "                                                 signer-* le fait tout seul (--publier : synchronise juste apr\xC3\xA8s)\n"
           "  github-config --depot PROPRIETAIRE/NOM [--branche main] [--racine DOSSIER]\n"
           "                                                 le d\xC3\xA9p\xC3\xB4t ; la premi\xC3\xA8re fois, cr\xC3\xA9" "e la cl\xC3\xA9 de synchro et montre son CODE DE SECOURS\n"
           "  github-config --depot ... --code-secours XPGS1-... [--remplacer]\n"
           "                                                 un autre poste (ou apr\xC3\xA8s une panne) : redonne la cl\xC3\xA9 de synchro ;\n"
           "                                                 --remplacer : une autre cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 est mise de c\xC3\xB4t\xC3\xA9 (jamais effac\xC3\xA9" "e)\n"
           "  github-jeton [--variable NOM]                  enregistre le jeton GitHub (tap\xC3\xA9 sans \xC3\xA9" "cho, ou lu dans la variable NOM),\n"
           "                                                 prot\xC3\xA9g\xC3\xA9 pour ce compte Windows ; --effacer le retire\n"
           "  github-etat [--hors-ligne]                     la configuration, puis ce qui serait re\xC3\xA7u et envoy\xC3\xA9 (sans rien changer)\n"
           "  github-synchroniser [--recevoir-seulement]     re\xC3\xA7oit ce que le d\xC3\xA9p\xC3\xB4t a de plus r\xC3\xA9" "cent, puis publie le reste (un commit)\n"
           "  github-code-secours --afficher                 remontre le code de secours de la cl\xC3\xA9 de synchro\n"
           "  --version\n\n"
           "--dossier : le dossier de la cl\xC3\xA9 et du registre des activations (par d\xC3\xA9" "faut " + defaultKeyFolder() + ").\n"
           "Le registre des activations (activations.jsonl) : chaque licence sign\xC3\xA9" "e, chaque demande enregistr\xC3\xA9" "e, chaque r\xC3\xA9vocation ;\n"
           "seulement ce que les clients t'envoient (rien de leur mat\xC3\xA9riel). Voir chaque lancement demandera le service en ligne.\n"
           "Code de sortie : 0 fait, 1 refus\xC3\xA9, 2 mauvais usage, 3 sign\xC3\xA9 et rang\xC3\xA9 mais pas publi\xC3\xA9 (relance github-synchroniser).\n";
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

// Une ligne tapee sans echo (le jeton) ; lue telle quelle si l'entree n'est pas une console (un tube).
std::string readSecretLine() {
    std::string line;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool console = h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode);
    if (console) SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
    std::getline(std::cin, line);
    if (console) {
        SetConsoleMode(h, mode);
        std::fputs("\n", stderr);
    }
#else
    termios old{};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios quiet = old;
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
    }
    std::getline(std::cin, line);
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &old);
        std::fputs("\n", stderr);
    }
#endif
    return line;
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
    static const std::vector<std::string> kFlags = {"--essai",     "--garder",    "--vide",  "--publier", "--effacer", "--hors-ligne",
                                                    "--recevoir-seulement", "--afficher", "--remplacer"};
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
    // Le registre des activations : les lignes d'une signature (un echec ne defait pas la signature : il est dit).
    const auto noteActivations = [&](const std::vector<ActivationRecord>& records) {
        if (records.empty()) return;
        std::string w;
        if (appendActivations(folder, records, &w))
            say("Registre des activations : " + std::to_string(records.size()) + " ligne(s) ajout\xC3\xA9" "e(s) (" + activationsPath(folder) + ")\n");
        else
            complain("ATTENTION : le registre des activations n'a pas \xC3\xA9t\xC3\xA9 mis \xC3\xA0 jour (" + w + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    };
    const auto showKey = [&](const SigningKey& key) {
        say("Cl\xC3\xA9 de l'\xC3\xA9" "diteur : " + key.name() + "\n  identifiant " + key.keyId() + "\n  empreinte   " + key.fingerprint(0) + "\n  fichier     " + key.path()
            + "\n  protection  " + (crypto::protectedHere() ? "DPAPI (ce compte Windows)" : "fichier lisible par son seul propri\xC3\xA9taire (pas de DPAPI ici)") + "\n"
            + (key.test() ? "  !!! CL\xC3\x89 D'ESSAI : jamais pour des licences de clients ; un programme qui l'embarque n'est pas livrable !!!\n" : ""));
    };

    // 1.31.0 : ranger un fichier signe dans l'archive du poste (un echec ne defait pas la signature : il est dit).
    const auto archive = [&](const std::string& bytes, const SigningKey& key) {
        std::string what, w;
        if (github::archiveSigned(folder, bytes, github::trustedKeys(&key), &what, &w))
            say("Archive : " + what + " (" + github::archiveFolder(folder) + ")\n");
        else
            complain("ATTENTION : le fichier n'a pas \xC3\xA9t\xC3\xA9 rang\xC3\xA9 dans l'archive (" + w + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    };
    const auto syncReport = [&](const SigningKey* key, bool receiveOnly, bool dryRun) -> int {
        github::SyncOptions o;
        o.receiveOnly = receiveOnly;
        o.dryRun = dryRun;
        o.now = licensing::systemNow();
        o.tool = std::string("XPG-License-Admin ") + kToolVersion;
        const auto r = github::synchronize(folder, key, o);
        std::string text = r.text();
        if (r.ok) {
            say(text);
        } else {
            if (!text.empty() && text.back() == '\n') text.pop_back();
            complain(text);
        }
        return r.ok ? 0 : 1;
    };
    // Apres une signature : synchroniser (--publier), ou le rappeler si la synchro est configuree.
    const auto afterSigning = [&](const SigningKey& key) -> int {
        if (a.flag("--publier")) {
            say("\nSynchro GitHub (--publier) :\n");
            if (syncReport(&key, false, false) == 0) return 0;
            complain("Le fichier est sign\xC3\xA9 et rang\xC3\xA9 dans l'archive ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : ne signe pas de nouveau, relance github-synchroniser.");
            return 3;
        }
        if (github::configured(folder)) say("\xC3\x80 publier dans le d\xC3\xA9p\xC3\xB4t GitHub : XPG-License-Admin github-synchroniser\n");
        return 0;
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
        const bool empty = a.flag("--vide"), keep = a.flag("--garder");
        if (empty && keep) {
            complain("exporter-cle : --vide et --garder ensemble ? choisis l'un ou l'autre");
            return 2;
        }
        std::unique_ptr<SigningKey> key;
        if (!empty) {
            key = keyOrFail();
            if (!key) return 1;
        }
        // Le fichier actuel : ses cles sont gardees avec --garder, et dites quand elles en sortent.
        const std::string outPath = a.one("--sortie");
        std::string existing;
        std::error_code ec;
        if (std::filesystem::is_regular_file(std::filesystem::path(std::u8string(outPath.begin(), outPath.end())), ec) && !readText(outPath, existing, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string text, summary;
        if (!exportPublisherKeys(key.get(), existing, outPath, keep, empty, now, text, &why, &summary) || !writeText(outPath, text, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9" "s publiques \xC3\xA9" "crites : " + outPath + "\n" + summary
            + (key && key->test() ? "!!! CL\xC3\x89 D'ESSAI : compil\xC3\xA9" "e seulement avec l'option CMake XPG_LICENCES_CLE_ESSAI, jamais livr\xC3\xA9" "e !!!\n" : ""));
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
        std::vector<ActivationRecord> records;
        const bool ok = a.command == "signer-registre" ? signRegistry(*key, input, now, out, &why) : signRevocations(*key, input, now, out, &why, &records);
        if (!ok || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        std::string text;
        (void)describeFile(out, key.get(), text);
        say("Sign\xC3\xA9 : " + a.one("--sortie") + "\n" + text);
        noteActivations(records);
        archive(out, *key);
        return afterSigning(*key);
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
        // 1.33.0 : retirer des PC de la licence etendue (--licence) : un PC remplace ou rendu.
        if (auto drop = a.options.find("--retirer-pc"); drop != a.options.end()) in.dropDevices = drop->second;
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
        std::vector<ActivationRecord> records;
        if (!signLicense(*key, in, now, out, &why, &summary, &records) || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sign\xC3\xA9" "e : " + a.one("--sortie") + "\n" + summary + (key->test() ? "!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI !!!\n" : ""));
        noteActivations(records);
        archive(out, *key);
        return afterSigning(*key);
    }
    if (a.command == "enregistrer-demande") {
        if (a.positional.empty()) {
            complain("enregistrer-demande : quelle demande (.xpgreq) ?\n\n" + usage());
            return 2;
        }
        std::vector<ActivationRecord> records;
        for (const auto& path : a.positional) {
            std::string bytes;
            ActivationRecord r;
            if (!readText(path, bytes, why) || !requestRecord(bytes, now, r, &why)) {
                complain("Refus\xC3\xA9 (" + path + ") : " + why);
                return 1;
            }
            say("Demande enregistr\xC3\xA9" "e : PC " + (r.deviceName.empty() ? std::string("(sans nom)") : "\xC2\xAB " + r.deviceName + " \xC2\xBB") + " (" + r.fingerprint
                + "), demand\xC3\xA9" "e le " + licensing::isoTime(r.requestedAt) + (r.licenseId.empty() ? "" : ", licence " + r.licenseId)
                + (r.software.empty() ? "" : ", " + r.software) + "\n");
            records.push_back(std::move(r));
        }
        if (!appendActivations(folder, records, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Registre des activations : " + activationsPath(folder) + "\n");
        if (a.flag("--publier")) {
            std::string kw;
            auto key = loadPublisherKey(folder, &kw);
            say("\nSynchro GitHub (--publier) :\n");
            if (syncReport(key.get(), false, false) == 0) return 0;
            complain("La demande est not\xC3\xA9" "e ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : relance github-synchroniser.");
            return 3;
        }
        if (github::configured(folder)) say("\xC3\x80 publier dans le d\xC3\xA9p\xC3\xB4t GitHub : XPG-License-Admin github-synchroniser\n");
        return 0;
    }
    if (a.command == "liste") {
        std::vector<ActivationRecord> records;
        int skipped = 0;
        if (!readActivations(folder, records, &skipped, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        const auto devices = filterDevices(knownDevices(records, now), a.one("--licence"), a.one("--client"));
        std::string head = "Registre des activations : " + activationsPath(folder) + " (le " + licensing::isoDate(now) + ")";
        if (!a.one("--licence").empty()) head += ", licence " + a.one("--licence");
        if (!a.one("--client").empty()) head += ", client \xC2\xAB " + a.one("--client") + " \xC2\xBB";
        say(head + "\n\n" + devicesTable(devices));
        if (skipped > 0) complain("Attention : " + std::to_string(skipped) + " ligne(s) illisible(s) du registre ignor\xC3\xA9" "e(s).");
        if (!a.one("--csv").empty()) {
            if (!writeText(a.one("--csv"), devicesCsv(devices), why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("CSV \xC3\xA9" "crit : " + a.one("--csv") + "\n");
        }
        return 0;
    }
    // 1.32.0 : la phrase de passe d'une sauvegarde (jamais affichee) : la variable donnee, ou tapee sans echo.
    const auto readPhrase = [&](const char* prompt, std::string& phrase) -> bool {
        if (!a.one("--phrase-variable").empty()) {
            const char* v = std::getenv(a.one("--phrase-variable").c_str());
            if (!v || !*v) {
                complain("Refus\xC3\xA9 : la variable " + a.one("--phrase-variable") + " est vide ou absente");
                return false;
            }
            phrase = v;
            return true;
        }
        complain(prompt);
        phrase = readSecretLine();
        while (!phrase.empty() && (phrase.back() == '\r' || phrase.back() == '\n')) phrase.pop_back();
        return true;
    };
    const auto wipeText = [](std::string& s) {
        std::fill(s.begin(), s.end(), '\0');
        s.clear();
    };
    if (a.command == "sauvegarder-cle") {
        if (!need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        std::string p1, p2, out;
        if (!readPhrase("Phrase de passe de la sauvegarde (12 caract\xC3\xA8res au moins ; elle ne s'affiche pas) : ", p1)) return 1;
        if (a.one("--phrase-variable").empty()) {
            if (!readPhrase("La m\xC3\xAAme phrase, une seconde fois : ", p2)) return 1;
            if (p1 != p2) {
                wipeText(p1);
                wipeText(p2);
                complain("Refus\xC3\xA9 : les deux phrases ne sont pas les m\xC3\xAAmes ; rien n'est \xC3\xA9" "crit.");
                return 1;
            }
            wipeText(p2);
        }
        say("Chiffrement de la sauvegarde (Argon2id : quelques secondes, 256 Mio de m\xC3\xA9moire)...\n");
        const bool ok = writeKeyBackup(*key, p1, out, &why);
        wipeText(p1);
        if (!ok || !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sauvegarde \xC3\xA9" "crite : " + a.one("--sortie") + "\n  cl\xC3\xA9 " + key->keyId() + (key->test() ? " (CL\xC3\x89 D'ESSAI)" : "") + "\n"
            "Range-la HORS de ce PC (cl\xC3\xA9 USB au coffre, un second support) ; note la phrase \xC3\xA0 part (gestionnaire de mots de passe).\n"
            "Sans la phrase, la sauvegarde ne sert \xC3\xA0 rien ; avec elle, n'importe qui peut signer des licences : garde les deux s\xC3\xA9par\xC3\xA9ment.\n"
            "V\xC3\xA9rifie-la maintenant : XPG-License-Admin verifier-sauvegarde " + a.one("--sortie") + "\n");
        return 0;
    }
    if (a.command == "verifier-sauvegarde" || a.command == "restaurer-cle") {
        const bool restore = a.command == "restaurer-cle";
        const std::string file = restore ? a.one("--entree") : (a.positional.empty() ? std::string() : a.positional[0]);
        if (file.empty()) {
            complain(a.command + " : quelle sauvegarde (.xpgcle) ?\n\n" + usage());
            return 2;
        }
        std::string bytes, keyId, text, phrase, name;
        if (!readText(file, bytes, why) || !describeKeyBackup(bytes, keyId, text, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say(file + " : " + text + "\n");
        if (!readPhrase("Phrase de passe de la sauvegarde (elle ne s'affiche pas) : ", phrase)) return 1;
        if (!restore) {
            const bool ok = checkKeyBackup(bytes, phrase, keyId, name, &why);
            wipeText(phrase);
            if (!ok) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Bonne : la phrase ouvre la sauvegarde de la cl\xC3\xA9 " + keyId + (name.empty() ? std::string() : " (" + name + ")") + ".\n");
            return 0;
        }
        auto key = restoreKeyBackup(folder, bytes, phrase, &why);
        wipeText(phrase);
        if (!key) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9 restaur\xC3\xA9" "e sur ce PC.\n");
        showKey(*key);
        return 0;
    }
    if (a.command == "archiver") {
        if (a.positional.empty()) {
            complain("archiver : quels fichiers sign\xC3\xA9s (registre, r\xC3\xA9vocations, licences) ?\n\n" + usage());
            return 2;
        }
        auto key = keyOrFail();
        if (!key) return 1;
        int refused = 0;
        for (const auto& path : a.positional) {
            std::string bytes, what, w;
            if (!readText(path, bytes, w) || !github::archiveSigned(folder, bytes, github::trustedKeys(key.get()), &what, &w)) {
                complain("Refus\xC3\xA9 (" + path + ") : " + w);
                ++refused;
                continue;
            }
            say(path + " : " + what + "\n");
        }
        say("Archive : " + github::archiveFolder(folder) + "\n");
        if (refused == 0 && github::configured(folder)) say("\xC3\x80 publier dans le d\xC3\xA9p\xC3\xB4t GitHub : XPG-License-Admin github-synchroniser\n");
        return refused ? 1 : 0;
    }
    if (a.command == "github-config") {
        github::Config c;
        const bool had = github::configured(folder);
        if (had && !github::readConfig(folder, c, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (a.one("--depot").empty() && !had) {
            complain("github-config : l'option --depot PROPRIETAIRE/NOM manque\n\n" + usage());
            return 2;
        }
        if (!a.one("--depot").empty()) c.repo = a.one("--depot");
        if (!a.one("--branche").empty()) c.branch = a.one("--branche");
        if (a.options.count("--racine")) c.root = a.one("--racine");
        if (!a.one("--api").empty()) c.api = a.one("--api");
        github::SyncKey sk{};
        std::string restored, kept;
        if (!a.one("--code-secours").empty()) {   // d'abord la cle : un code refuse ne change rien
            if (!github::restoreSyncKey(folder, a.one("--code-secours"), sk, &why, a.flag("--remplacer"), &kept)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            restored = "Cl\xC3\xA9 de synchro redonn\xC3\xA9" "e par le code de secours : " + github::syncKeyId(sk) + "\n"
                       + (kept.empty() ? std::string() : "  (l'ancienne est gard\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9 : " + kept + ")\n");
        }
        if (!github::writeConfig(folder, c, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Synchro GitHub : d\xC3\xA9p\xC3\xB4t " + c.repo + ", branche " + c.branch + (c.root.empty() ? std::string() : ", dossier " + c.root)
            + (c.api == github::kDefaultApi ? std::string() : ", API " + c.api) + "\n  (" + github::configPath(folder) + ")\n");
        if (!restored.empty()) {
            say(restored);
        } else if (github::hasSyncKey(folder)) {
            if (!github::loadSyncKey(folder, sk, &why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro d\xC3\xA9j\xC3\xA0 sur ce poste : " + github::syncKeyId(sk) + "\n");
        } else {
            if (!github::createSyncKey(folder, sk, &why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro cr\xC3\xA9\xC3\xA9" "e : " + github::syncKeyId(sk) + "\n"
                "  (Ce d\xC3\xA9p\xC3\xB4t a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 synchronis\xC3\xA9 par un autre PC ? Il te faut SON code de secours :\n"
                "   github-config --depot " + c.repo + " --code-secours XPGS1-... --remplacer)\n\n"
                "  !!! NOTE CE CODE DE SECOURS, hors de ce PC (gestionnaire de mots de passe, papier au coffre) !!!\n\n"
                "      " + github::recoveryCode(sk) + "\n\n"
                "  Sans lui, apr\xC3\xA8s une panne de ce PC, les licences et le registre des activations du d\xC3\xA9p\xC3\xB4t restent illisibles.\n"
                "  Ne le mets jamais dans le d\xC3\xA9p\xC3\xB4t, ni dans un courriel. github-code-secours --afficher le remontre.\n");
        }
        crypto::wipe(sk);
        if (!github::hasToken(folder)) say("Ensuite : XPG-License-Admin github-jeton (le jeton GitHub de ce d\xC3\xA9p\xC3\xB4t).\n");
        else say("Ensuite : XPG-License-Admin github-etat, puis github-synchroniser.\n");
        return 0;
    }
    if (a.command == "github-jeton") {
        if (a.flag("--effacer")) {
            if (!github::eraseToken(folder, &why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Jeton GitHub retir\xC3\xA9 de ce poste. (Pense aussi \xC3\xA0 le r\xC3\xA9voquer sur GitHub : Settings > Developer settings.)\n");
            return 0;
        }
        std::string token;
        if (!a.one("--variable").empty()) {
            const char* v = std::getenv(a.one("--variable").c_str());
            if (!v || !*v) {
                complain("Refus\xC3\xA9 : la variable " + a.one("--variable") + " est vide ou absente");
                return 1;
            }
            token = v;
        } else {
            complain("Colle le jeton GitHub puis Entr\xC3\xA9" "e (il ne s'affiche pas) : ");
            token = readSecretLine();
        }
        while (!token.empty() && (token.back() == '\r' || token.back() == '\n' || token.back() == ' ')) token.pop_back();
        while (!token.empty() && token.front() == ' ') token.erase(0, 1);
        const std::string kind = github::tokenKind(token);
        const bool ok = github::saveToken(folder, token, &why);
        std::fill(token.begin(), token.end(), '\0');
        if (!ok) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Jeton GitHub enregistr\xC3\xA9" + (kind.empty() ? std::string() : " (" + kind + ")") + ", prot\xC3\xA9g\xC3\xA9 pour ce compte "
            + (crypto::protectedHere() ? "Windows (DPAPI)" : "(fichier lisible par son seul propri\xC3\xA9taire)") + ".\n"
            + (kind.empty() ? "Attention : ce jeton n'a pas une forme connue (github_pat_..., ghp_...).\n" : "")
            + "Ensuite : XPG-License-Admin github-etat\n");
        return 0;
    }
    if (a.command == "github-code-secours") {
        github::SyncKey sk{};
        if (!github::loadSyncKey(folder, sk, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (!a.flag("--afficher")) {
            say("Cl\xC3\xA9 de synchro : " + github::syncKeyId(sk) + "\nLe code de secours ne s'affiche qu'avec --afficher (personne derri\xC3\xA8re toi ?).\n");
            crypto::wipe(sk);
            return 0;
        }
        say("Cl\xC3\xA9 de synchro " + github::syncKeyId(sk) + "\nCode de secours :\n\n      " + github::recoveryCode(sk) + "\n\n"
            "Garde-le hors de ce PC ; ne le mets jamais dans le d\xC3\xA9p\xC3\xB4t.\n");
        crypto::wipe(sk);
        return 0;
    }
    if (a.command == "github-etat") {
        github::Config c;
        if (!github::readConfig(folder, c, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Synchro GitHub : d\xC3\xA9p\xC3\xB4t " + c.repo + ", branche " + c.branch + (c.root.empty() ? std::string() : ", dossier " + c.root)
            + (c.api == github::kDefaultApi ? std::string() : ", API " + c.api) + "\n");
        std::string w;
        std::string token;
        const bool tokenOk = github::loadToken(folder, token, &w);
        const std::string kind = tokenOk ? github::tokenKind(token) : std::string();
        std::fill(token.begin(), token.end(), '\0');
        say(std::string("  jeton           ") + (tokenOk ? "enregistr\xC3\xA9" + (kind.empty() ? std::string() : " (" + kind + ")") : "non : " + w) + "\n");
        github::SyncKey sk{};
        const bool skOk = github::loadSyncKey(folder, sk, &w);
        say(std::string("  cl\xC3\xA9 de synchro  ") + (skOk ? github::syncKeyId(sk) : "non : " + w) + "\n");
        crypto::wipe(sk);
        std::string kw;
        auto key = loadPublisherKey(folder, &kw);
        say(std::string("  cl\xC3\xA9 d'\xC3\xA9" "diteur   ") + (key ? key->keyId() + " (les signatures re\xC3\xA7ues sont v\xC3\xA9rifi\xC3\xA9" "es)" : "aucune sur ce poste (les signatures re\xC3\xA7ues ne sont v\xC3\xA9rifi\xC3\xA9" "es que par les cl\xC3\xA9s compil\xC3\xA9" "es)") + "\n");
        say("  archive         " + github::archiveFolder(folder) + "\n");
        if (a.flag("--hors-ligne") || !tokenOk || !skOk) return tokenOk && skOk ? 0 : 1;
        say("\nCe que ferait github-synchroniser (rien n'est chang\xC3\xA9) :\n");
        return syncReport(key.get(), false, true);
    }
    if (a.command == "github-synchroniser") {
        std::string kw;
        auto key = loadPublisherKey(folder, &kw);
        if (!key && !a.flag("--recevoir-seulement"))
            say("(Pas de cl\xC3\xA9 d'\xC3\xA9" "diteur sur ce poste : les signatures re\xC3\xA7ues ne sont v\xC3\xA9rifi\xC3\xA9" "es que par les cl\xC3\xA9s compil\xC3\xA9" "es dans l'outil.)\n");
        return syncReport(key.get(), a.flag("--recevoir-seulement"), false);
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
    core::hardenLibrarySearch();   // 1.32.0 : aucune DLL du dossier courant ni du PATH
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
