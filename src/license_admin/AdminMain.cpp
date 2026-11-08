// =============================================================================
//  license_admin/AdminMain.cpp - 1.30.0 (prepare en 1.29.0) : XPG-License-Admin,
//  l'outil de l'EDITEUR pour les licences, en console
// -----------------------------------------------------------------------------
//  JAMAIS LIVRE : cible a part (xpg_license_admin), construite sur le poste Admin
//  seulement ; ni dans l'installateur, ni dans un zip (outils/mingw/package_mingw.py
//  arrete la construction s'il trouve son nom ou sa marque dans le dossier a
//  livrer). Voir docs/LICENCES.md et docs/SERVICE-LICENCES.md.
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
//  1.32.0 : LA SAUVEGARDE DE LA CLE PAR UNE PHRASE DE PASSE (KeyBackup.hpp) - relisible sur un autre PC :
//    XPG-License-Admin sauvegarder-cle --sortie F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin verifier-sauvegarde F.xpgcle [--phrase-variable NOM]
//    XPG-License-Admin restaurer-cle --entree F.xpgcle [--phrase-variable NOM] [--dossier D]
//  La phrase se tape sans echo (deux fois pour sauvegarder) ; --phrase-variable la lit dans une variable
//  d'environnement (les essais, un script). Elle n'est jamais affichee ni ecrite.
//  1.30.0 : le registre des activations (activations.jsonl, dans le dossier de la cle) : signer-licence,
//  signer-revocations et enregistrer-demande y ajoutent leurs lignes ; liste montre tous les PC connus.
//  1.31.0 : l'ARCHIVE du poste (chaque fichier signe y est range). 1.35.0 : l'archive se
//  synchronise par le SERVICE DES LICENCES (ArchiveSync.hpp), qui recoit aussi les demandes des clients, active les
//  licences en ligne par un code, tient les licences flottantes (ServiceClient.hpp, docs/SERVICE-LICENCES.md) :
//    XPG-License-Admin archiver FICHIER... [--dossier D]
//    XPG-License-Admin archive-cle [--code-secours CODE [--remplacer]]
//    XPG-License-Admin archive-code-secours --afficher
//    XPG-License-Admin archive-synchroniser [--recevoir-seulement] [--a-blanc]
//    XPG-License-Admin service-config --url https://...
//    XPG-License-Admin service-verifier
//    XPG-License-Admin service-certifier
//    XPG-License-Admin service-registre
//    XPG-License-Admin service-revocations [--entree F.xpgrev]
//    XPG-License-Admin licence-en-ligne --parametres P.json [--bail-jours N] [--sortie L.xpglic] [--etendre [--nouveau-code]]
//    XPG-License-Admin service-licences
//    XPG-License-Admin service-retirer --licence ID --poste CLE
//    XPG-License-Admin service-desactiver --licence ID [--reactiver]
//    XPG-License-Admin service-demandes [--toutes]
//    XPG-License-Admin service-refuser --numero N --raison TEXTE
//    XPG-License-Admin service-publier-licence --licence F.xpglic
//    XPG-License-Admin service-stats
//    XPG-License-Admin kit-studios --sortie DOSSIER      (les fichiers PUBLICS a compiler dans les Studios)
//    XPG-License-Admin --version
//  Les commandes github-* et passerelle-* (1.31.0 a 1.34.0) n'existent plus : le service des licences les remplace.
//  LE CODE D'ACTIVATION (licence-en-ligne) est montre UNE fois, a l'ecran, puis oublie : le service n'en garde que
//  l'empreinte ; il n'est ecrit dans aucun fichier ni journal.
//  Code de sortie : 0 fait ; 1 refuse (la raison est ecrite) ; 2 mauvais usage ; 3 signe et range, mais la
//  publication (--publier) a echoue (ne pas signer de nouveau : relancer archive-synchroniser).
// =============================================================================
#include "Admin.hpp"
#include "ArchiveSync.hpp"
#include "KeyBackup.hpp"
#include "Mail.hpp"   // 1.37.0 : le mail au client
#include "ServiceClient.hpp"

#include "../core/SystemOpen.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/Service.hpp"

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

constexpr const char* kToolVersion = "1.3.0";   // 1.31.0 : l'archive ; 1.34.0 : la passerelle ; 1.35.0 : le service des licences

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
           "                 une licence LI\xC3\x89" "E \xC3\xA0 ces PC (un fichier \xC3\xA0 leur envoyer) ; en ligne, sans PC : licence-en-ligne\n"
           "  verifier FICHIER                               lit un fichier sign\xC3\xA9 et dit ce qu'il contient\n"
           "  enregistrer-demande PC.xpgreq                  note une demande re\xC3\xA7ue (pas encore sign\xC3\xA9" "e) dans le registre des activations\n"
           "  liste [--licence ID] [--client TEXTE] [--csv F.csv]\n"
           "                                                 tous les PC connus (un par ligne, leur dernier \xC3\xA9tat), les totaux ;\n"
           "                                                 --csv : le m\xC3\xAAme tableau pour Excel (UTF-8, s\xC3\xA9parateur ;)\n"
           "\nL'archive du poste (les licences, le registre, les r\xC3\xA9vocations, le registre des activations) :\n"
           "  archiver FICHIER...                            range des fichiers sign\xC3\xA9s (registre, r\xC3\xA9vocations, licences) dans l'archive ;\n"
           "                                                 signer-* le fait tout seul (--publier : synchronise juste apr\xC3\xA8s)\n"
           "  archive-cle                                    cr\xC3\xA9" "e la cl\xC3\xA9 de synchro de l'archive et montre son CODE DE SECOURS (une fois)\n"
           "  archive-cle --code-secours XPGS1-... [--remplacer]\n"
           "                                                 un autre poste (ou apr\xC3\xA8s une panne) : redonne la cl\xC3\xA9 de synchro ;\n"
           "                                                 --remplacer : une autre cl\xC3\xA9 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 est mise de c\xC3\xB4t\xC3\xA9 (jamais effac\xC3\xA9" "e)\n"
           "  archive-code-secours --afficher                remontre le code de secours de la cl\xC3\xA9 de synchro\n"
           "  archive-synchroniser [--recevoir-seulement] [--a-blanc]\n"
           "                                                 re\xC3\xA7oit ce que l'archive en ligne (le service) a de plus r\xC3\xA9" "cent, puis envoie\n"
           "                                                 le reste, chiffr\xC3\xA9 ; --a-blanc : dit ce qui serait fait, sans rien changer\n"
           "\nLe service des licences (ton Worker Cloudflare ; outils/service/LISEZ-MOI-SERVICE.txt pour le mettre en ligne) :\n"
           "  service-config --url https://NOM.COMPTE.workers.dev\n"
           "                                                 son adresse (http:// seulement vers ce poste, pour les essais)\n"
           "  service-verifier                               il r\xC3\xA9pond, ta cl\xC3\xA9 est accept\xC3\xA9" "e, sa cl\xC3\xA9 et son certificat\n"
           "  service-certifier                              montre l'empreinte de la cl\xC3\xA9 du service et la certifie (un an) ;\n"
           "                                                 relance-la avant la fin pour renouveler\n"
           "  service-registre                               d\xC3\xA9pose le registre de l'archive (s'il est plus r\xC3\xA9" "cent)\n"
           "  service-revocations [--entree F.xpgrev]        d\xC3\xA9pose la liste de r\xC3\xA9vocation (PRIV\xC3\x89" "E : seuls les PC concern\xC3\xA9s la re\xC3\xA7oivent)\n"
           "  licence-en-ligne --parametres P.json [--bail-jours N] [--sortie L.xpglic]\n"
           "                                                 signe une licence EN LIGNE (aucun PC : chaque PC l'active avec un code),\n"
           "                                                 la d\xC3\xA9pose avec l'empreinte d'un CODE D'ACTIVATION neuf, et montre ce code\n"
           "                                                 UNE FOIS (note-le pour ton client : il n'est gard\xC3\xA9 nulle part) ;\n"
           "                                                 P.json : comme signer-licence, \"type\" peut \xC3\xAAtre \"floating\" (flottante) ;\n"
           "                                                 --bail-jours : la dur\xC3\xA9" "e d'une activation sans connexion (30 par d\xC3\xA9" "faut)\n"
           "  licence-en-ligne --parametres P.json --etendre [--nouveau-code]\n"
           "                                                 une \xC3\xA9mission plus r\xC3\xA9" "cente (prolonger, plus de postes) ; le m\xC3\xAAme code,\n"
           "                                                 ou un neuf avec --nouveau-code (l'ancien ne marche plus)\n"
           "  service-licences                               les licences en ligne, leurs PC (vu le, bail jusqu'au) et jetons flottants\n"
           "  service-retirer --licence ID --poste CLE       ce PC n'est plus renouvel\xC3\xA9 (sa place se lib\xC3\xA8re)\n"
           "  service-desactiver --licence ID [--reactiver]  la licence ne s'active plus et ses PC ne sont plus renouvel\xC3\xA9s\n"
           "  service-demandes [--toutes]                    rel\xC3\xA8ve les demandes manuelles (rang\xC3\xA9" "es dans demandes-en-ligne/)\n"
           "  service-refuser --numero N --raison TEXTE      refuse cette demande (la raison scell\xC3\xA9" "e pour le PC)\n"
           "  service-publier-licence --licence F.xpglic     une licence li\xC3\xA9" "e \xC3\xA0 des PC : \xC3\xA0 chacun (sa demande en attente : accept\xC3\xA9" "e)\n"
           "  service-stats                                  les compteurs du service\n"
           "  code-licence --licence ID                      1.37.0 : revoir le code d'activation d'une licence en ligne (gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste)\n"
           "  mail-config --hote H --expediteur A [--port P] [--securite starttls|tls] [--nom N] [--identifiant I]\n"
           "                                                 1.37.0 : le mail au client \xC3\xA0 l'acceptation (ta bo\xC3\xAEte ; l'application l'envoie)\n"
           "  mail-mot-de-passe                              le mot de passe d'application, tap\xC3\xA9 sans \xC3\xA9" "cho (gard\xC3\xA9 chiffr\xC3\xA9)\n"
           "  mail-essai [--a ADRESSE]                       un mail d'essai (par d\xC3\xA9" "faut \xC3\xA0 ton adresse d'exp\xC3\xA9" "dition)\n"
           "  mail-oublier                                   retire le r\xC3\xA9glage du mail et son mot de passe\n"
           "  kit-studios --sortie DOSSIER                   PublisherKey.hpp, registre.xpgreg, ServiceAddress.hpp, cle-publique.txt\n"
           "                                                 (et LISEZ-MOI.txt) : les fichiers PUBLICS des Studios (cl\xC3\xA9 d'essai refus\xC3\xA9" "e)\n"
           "  --version\n\n"
           "Les commandes github-* et passerelle-* (1.31 \xC3\xA0 1.34) n'existent plus : le service des licences les remplace.\n"
           "--dossier : le dossier de la cl\xC3\xA9 et du registre des activations (par d\xC3\xA9" "faut " + defaultKeyFolder() + ").\n"
           "Le registre des activations (activations.jsonl) : chaque licence sign\xC3\xA9" "e, chaque demande enregistr\xC3\xA9" "e, chaque r\xC3\xA9vocation ;\n"
           "seulement ce que les clients t'envoient (rien de leur mat\xC3\xA9riel).\n"
           "Code de sortie : 0 fait, 1 refus\xC3\xA9, 2 mauvais usage, 3 sign\xC3\xA9 et rang\xC3\xA9 mais pas publi\xC3\xA9 (relance archive-synchroniser).\n";
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
    static const std::vector<std::string> kFlags = {"--essai",     "--garder",    "--vide",     "--publier",     "--afficher", "--remplacer",
                                                    "--recevoir-seulement", "--a-blanc", "--toutes", "--etendre", "--nouveau-code", "--reactiver"};
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
    const auto archiveFile = [&](const std::string& bytes, const SigningKey& key) {
        std::string what, w;
        if (archive::archiveSigned(folder, bytes, archive::trustedKeys(&key), &what, &w))
            say("Archive : " + what + " (" + archive::archiveFolder(folder) + ")\n");
        else
            complain("ATTENTION : le fichier n'a pas \xC3\xA9t\xC3\xA9 rang\xC3\xA9 dans l'archive (" + w + ") ; le fichier sign\xC3\xA9 est bien \xC3\xA9" "crit.");
    };
    // 1.35.0 : la synchro de l'archive par le service (la cle de l'editeur signe les appels).
    const auto syncReport = [&](const SigningKey& key, bool receiveOnly, bool dryRun) -> int {
        archive::SyncOptions o;
        o.receiveOnly = receiveOnly;
        o.dryRun = dryRun;
        o.now = licensing::systemNow();
        o.tool = std::string("XPG-License-Admin ") + kToolVersion;
        const auto r = archive::synchronize(folder, key, o);
        std::string text = r.text();
        if (r.ok) {
            say(text);
        } else {
            if (!text.empty() && text.back() == '\n') text.pop_back();
            complain(text);
        }
        return r.ok ? 0 : 1;
    };
    const auto serviceOptions = [&] {
        online::Options o;
        o.now = licensing::systemNow();
        o.tool = std::string("XPG-License-Admin/") + kToolVersion;
        return o;
    };
    const auto showResult = [&](const online::Result& r) -> int {
        std::string text = r.text();
        if (r.ok) {
            say(text + "\n");
            return 0;
        }
        complain(text);
        return 1;
    };
    // Apres une signature (--publier) : l'archive synchronisee ; le registre ou la liste de revocation deposes aussi sur
    // le service. Sans --publier : le rappeler si le service est regle.
    const auto afterSigning = [&](const SigningKey& key, const char* kind) -> int {
        const bool registry = std::string(kind) == "registre", revocations = std::string(kind) == "revocations";
        if (!a.flag("--publier")) {
            if (online::configured(folder)) {
                say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser");
                if (registry) say(", puis service-registre");
                if (revocations) say(", puis service-revocations");
                say("\n");
            }
            return 0;
        }
        say("\nSynchro de l'archive (--publier) :\n");
        int rc = syncReport(key, false, false);
        if (rc == 0 && registry) rc = showResult(online::publishRegistry(folder, key, serviceOptions()));
        if (rc == 0 && revocations) rc = showResult(online::publishRevocations(folder, key, serviceOptions()));
        if (rc == 0) return 0;
        complain("Le fichier est sign\xC3\xA9 et rang\xC3\xA9 dans l'archive ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : ne signe pas de nouveau, relance archive-synchroniser"
                 + std::string(registry ? " et service-registre" : revocations ? " et service-revocations" : "") + ".");
        return 3;
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
        archiveFile(out, *key);
        return afterSigning(*key, a.command == "signer-registre" ? "registre" : "revocations");
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
        archiveFile(out, *key);
        // 1.35.0 : le service regle, la licence part a ses PC (et repond a leur demande) par une commande.
        if (online::configured(folder))
            say("\xC3\x80 publier \xC3\xA0 ses PC (chacun la r\xC3\xA9" "cup\xC3\xA8re seul) : XPG-License-Admin service-publier-licence --licence "
                + a.one("--sortie") + "\n");
        return afterSigning(*key, "licence");
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
            auto key = keyOrFail();
            if (!key) return 3;
            say("\nSynchro de l'archive (--publier) :\n");
            if (syncReport(*key, false, false) == 0) return 0;
            complain("La demande est not\xC3\xA9" "e ; seule la publication a \xC3\xA9" "chou\xC3\xA9 : relance archive-synchroniser.");
            return 3;
        }
        if (online::configured(folder)) say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser\n");
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
            if (!readText(path, bytes, w) || !archive::archiveSigned(folder, bytes, archive::trustedKeys(key.get()), &what, &w)) {
                complain("Refus\xC3\xA9 (" + path + ") : " + w);
                ++refused;
                continue;
            }
            say(path + " : " + what + "\n");
        }
        say("Archive : " + archive::archiveFolder(folder) + "\n");
        if (refused == 0 && online::configured(folder)) say("\xC3\x80 publier : XPG-License-Admin archive-synchroniser\n");
        return refused ? 1 : 0;
    }
    // ---- 1.35.0 : la cle de synchro de l'archive (ses fichiers partent chiffres par elle) ----
    if (a.command == "archive-cle") {
        archive::SyncKey sk{};
        if (!a.one("--code-secours").empty()) {
            std::string kept;
            if (!archive::restoreSyncKey(folder, a.one("--code-secours"), sk, &why, a.flag("--remplacer"), &kept)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro redonn\xC3\xA9" "e par le code de secours : " + archive::syncKeyId(sk) + "\n"
                + (kept.empty() ? std::string() : "  (l'ancienne est gard\xC3\xA9" "e \xC3\xA0 c\xC3\xB4t\xC3\xA9 : " + kept + ")\n") + "Ensuite : XPG-License-Admin archive-synchroniser --recevoir-seulement\n");
            crypto::wipe(sk);
            return 0;
        }
        if (archive::hasSyncKey(folder)) {
            if (!archive::loadSyncKey(folder, sk, &why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            say("Cl\xC3\xA9 de synchro d\xC3\xA9j\xC3\xA0 sur ce poste : " + archive::syncKeyId(sk) + " (archive-code-secours --afficher remontre son code).\n");
            crypto::wipe(sk);
            return 0;
        }
        if (!archive::createSyncKey(folder, sk, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Cl\xC3\xA9 de synchro cr\xC3\xA9\xC3\xA9" "e : " + archive::syncKeyId(sk) + "\n"
            "  (L'archive en ligne a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 envoy\xC3\xA9" "e par un autre PC ? Il te faut SON code de secours :\n"
            "   archive-cle --code-secours XPGS1-... --remplacer)\n\n"
            "  !!! NOTE CE CODE DE SECOURS, hors de ce PC (gestionnaire de mots de passe, papier au coffre) !!!\n\n"
            "      " + archive::recoveryCode(sk) + "\n\n"
            "  Sans lui, apr\xC3\xA8s une panne de ce PC, les licences et le registre des activations de l'archive en ligne restent illisibles.\n"
            "  Ne le mets jamais dans un fichier envoy\xC3\xA9, ni dans un courriel. archive-code-secours --afficher le remontre.\n");
        crypto::wipe(sk);
        return 0;
    }
    if (a.command == "archive-code-secours") {
        archive::SyncKey sk{};
        if (!archive::loadSyncKey(folder, sk, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (!a.flag("--afficher")) {
            say("Cl\xC3\xA9 de synchro : " + archive::syncKeyId(sk) + "\nLe code de secours ne s'affiche qu'avec --afficher (personne derri\xC3\xA8re toi ?).\n");
            crypto::wipe(sk);
            return 0;
        }
        say("Cl\xC3\xA9 de synchro " + archive::syncKeyId(sk) + "\nCode de secours :\n\n      " + archive::recoveryCode(sk) + "\n\n"
            "Garde-le hors de ce PC ; ne l'envoie jamais.\n");
        crypto::wipe(sk);
        return 0;
    }
    if (a.command == "archive-synchroniser") {
        auto key = keyOrFail();
        if (!key) return 1;
        return syncReport(*key, a.flag("--recevoir-seulement"), a.flag("--a-blanc"));
    }
    // ---- 1.35.0 : le service des licences ----
    if (a.command == "service-config") {
        if (!need("--url")) return 2;
        online::Config c;
        c.url = a.one("--url");
        if (!online::writeConfig(folder, c, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        online::Config back;
        (void)online::readConfig(folder, back, &why);
        say("Service des licences : " + back.url + "\n  (" + online::configPath(folder) + ")\nEnsuite : XPG-License-Admin service-verifier, puis service-certifier.\n");
        return 0;
    }
    if (a.command == "service-verifier" || a.command == "service-certifier" || a.command == "service-registre" || a.command == "service-revocations" ||
        a.command == "service-licences" || a.command == "service-retirer" || a.command == "service-desactiver" || a.command == "service-demandes" ||
        a.command == "service-refuser" || a.command == "service-publier-licence" || a.command == "service-stats" || a.command == "licence-en-ligne") {
        if (a.command == "service-retirer" && (!need("--licence") || !need("--poste"))) return 2;
        if (a.command == "service-desactiver" && !need("--licence")) return 2;
        if (a.command == "service-refuser" && (!need("--numero") || !need("--raison"))) return 2;
        if (a.command == "service-publier-licence" && !need("--licence")) return 2;
        if (a.command == "licence-en-ligne" && !need("--parametres")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        const online::Options o = serviceOptions();
        if (a.command == "service-verifier") return showResult(online::verify(folder, *key, o));
        if (a.command == "service-registre") return showResult(online::publishRegistry(folder, *key, o));
        if (a.command == "service-stats") return showResult(online::stats(folder, *key, o));
        if (a.command == "service-retirer") return showResult(online::retireDevice(folder, *key, a.one("--licence"), a.one("--poste"), o));
        if (a.command == "service-refuser") return showResult(online::refuse(folder, *key, a.one("--numero"), a.one("--raison"), o));
        if (a.command == "service-certifier") {
            std::string serviceKey;
            const online::Result r = online::readServiceKey(folder, *key, o, serviceKey);
            if (!r.ok) return showResult(r);
            say("Cl\xC3\xA9 du service : " + online::keyFingerprint(serviceKey) + "\n  (" + serviceKey + ")\n");
            return showResult(online::certify(folder, *key, o, serviceKey));
        }
        if (a.command == "service-revocations") {
            if (!a.one("--entree").empty()) {
                std::string bytes, what;
                if (!readText(a.one("--entree"), bytes, why) || !archive::archiveSigned(folder, bytes, archive::trustedKeys(key.get()), &what, &why)) {
                    complain("Refus\xC3\xA9 : " + why);
                    return 1;
                }
                say(a.one("--entree") + " : " + what + "\n");
            }
            return showResult(online::publishRevocations(folder, *key, o));
        }
        if (a.command == "service-publier-licence") {
            std::string text;
            if (!readText(a.one("--licence"), text, why)) {
                complain("Refus\xC3\xA9 : " + why);
                return 1;
            }
            return showResult(online::publishBoundLicence(folder, *key, text, false, o));
        }
        if (a.command == "service-desactiver") {
            return showResult(online::setActive(folder, *key, a.one("--licence"), a.flag("--reactiver"), 0, o));
        }
        if (a.command == "service-licences") {
            online::Snapshot s;
            const online::Result r = online::listLicences(folder, *key, o, s);
            const int rc = showResult(r);
            if (rc != 0) return rc;
            for (const auto& l : s.licences) {
                std::string line = "\n" + l.licenseId + "  " + (l.floating() ? "flottante" : l.type) + ", " + std::to_string(l.seats) +
                                   (l.floating() ? " postes en m\xC3\xAAme temps" : " PC au plus") + ", bail " + std::to_string(l.leaseDays) + " j" +
                                   (l.active ? std::string() : ", D\xC3\x89SACTIV\xC3\x89" "E") + (l.hasCode ? std::string() : ", sans code") + "\n  \xC3\xA9mission " +
                                   licensing::isoTime(l.issuedAt);
                if (l.activations.empty()) line += "\n  aucun PC activ\xC3\xA9";
                for (const auto& act : l.activations)
                    line += "\n  PC \xC2\xAB " + (act.name.empty() ? std::string("(sans nom)") : act.name) + " \xC2\xBB " + act.deviceKey + "\n     " + act.stateLabel() +
                            ", activ\xC3\xA9 le " + (act.activatedAt ? licensing::isoTime(act.activatedAt) : std::string("?")) + ", vu le " +
                            (act.seenAt ? licensing::isoTime(act.seenAt) : std::string("?")) + ", bail jusqu'au " + (act.until ? licensing::isoTime(act.until) : std::string("?"));
                for (const auto& t : l.leases)
                    line += "\n  jeton pris par " + t.deviceKey.substr(0, 16) + "... jusqu'\xC3\xA0 " + licensing::isoTime(t.until);
                say(line + "\n");
            }
            if (!s.licences.empty()) say("\nRetirer un PC : service-retirer --licence ID --poste CLE ; d\xC3\xA9sactiver : service-desactiver --licence ID.\n");
            return 0;
        }
        if (a.command == "service-demandes") {
            const online::Result r = online::fetchRequests(folder, *key, o);
            const int rc = showResult(r);
            const auto all = online::listRequests(folder);
            int hidden = 0;
            bool waiting = false, join = false;
            for (const auto& e : all) {
                waiting = waiting || e.waiting();
                join = join || (e.waiting() && e.joinRequest());
                if (!a.flag("--toutes") && !e.waiting()) {
                    ++hidden;
                    continue;
                }
                std::string line = "\nn\xC2\xB0 " + std::to_string(e.number) + "  " + (e.createdAt ? licensing::isoTime(e.createdAt) : std::string("?")) + "  " +
                                   online::requestStateLabel(e.state) + "\n  poste " + e.device;
                if (e.state != online::RequestState::Unreadable) {
                    line += "\n  client " + e.customer + (e.contact.empty() ? std::string() : " (" + e.contact + ")") + "\n  PC \xC2\xAB " +
                            (e.deviceName.empty() ? std::string("(sans nom)") : e.deviceName) + " \xC2\xBB " + e.fingerprint + (e.software.empty() ? std::string() : ", " + e.software) +
                            "\n  licence demand\xC3\xA9" "e " + (e.licenseId.empty() ? std::string("(nouvelle)") : e.licenseId) + "\n  demande " + e.requestPath;
                    if (!e.message.empty()) line += "\n  message " + e.message;
                    if (!e.answer.empty()) line += "\n  r\xC3\xA9ponse " + e.answer;
                    if (e.joinRequest() && e.waiting()) line += "\n  !! " + online::joinWarning(e);
                }
                for (const auto& p : e.problems) line += "\n  ! " + p;
                say(line + "\n");
            }
            if (hidden > 0) say("\n(" + std::to_string(hidden) + " demande(s) d\xC3\xA9j\xC3\xA0 trait\xC3\xA9" "e(s) : --toutes pour les voir)\n");
            if (waiting)
                say("\nAccepter : signer-licence --demande <le fichier demande> ... puis service-publier-licence --licence <la licence> ;\n"
                    "refuser : service-refuser --numero N --raison \"...\".\n");
            if (join)
                say("Une demande d'ajout \xC3\xA0 une licence existante (!!) : n'importe qui peut taper un identifiant de licence et un nom de client - "
                    "confirme avec ton client avant de l'accepter.\n");
            return rc;
        }
        // ---- licence-en-ligne : signer une licence SANS PC, la deposer avec l'empreinte d'un code neuf, montrer le code ----
        online::Config cfg;
        if (!online::readConfig(folder, cfg, &why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        int lease = a.flag("--etendre") ? 0 : online::kDefaultLeaseDays;   // etendre : le service garde son bail, sauf --bail-jours
        if (!a.one("--bail-jours").empty()) {
            const std::string t = a.one("--bail-jours");
            if (t.empty() || t.size() > 3 || !std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; }) || std::atoi(t.c_str()) < 1 ||
                std::atoi(t.c_str()) > 366) {
                complain("licence-en-ligne : --bail-jours va de 1 \xC3\xA0 366");
                return 2;
            }
            lease = std::atoi(t.c_str());
        }
        LicenseInputs in;
        if (!readText(a.one("--parametres"), in.params, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        in.service = cfg.url;
        in.registry = archive::archivedText(folder, "registre/registre.xpgreg");
        in.revocations = archive::archivedText(folder, "revocations/revocations.xpgrev");
        if (in.registry.empty()) {
            complain("Refus\xC3\xA9 : pas de registre sign\xC3\xA9 dans l'archive (signer-registre d'abord)");
            return 1;
        }
        core::json::Value p;
        (void)core::json::parse(in.params, p, nullptr);
        const std::string id = p.isObject() ? p.str("license_id") : std::string();
        const std::string existing = id.empty() ? std::string() : archive::archivedText(folder, "licences/" + archive::safeName(id) + ".xpglic");
        if (a.flag("--etendre")) {
            if (existing.empty()) {
                complain("Refus\xC3\xA9 : la licence " + id + " n'est pas dans l'archive : rien \xC3\xA0 \xC3\xA9tendre (sans --etendre : une licence neuve)");
                return 1;
            }
            in.existing = existing;
        } else if (!existing.empty()) {
            complain("Refus\xC3\xA9 : la licence " + id + " est d\xC3\xA9j\xC3\xA0 dans l'archive : --etendre pour une \xC3\xA9mission plus r\xC3\xA9" "cente (le m\xC3\xAAme code, ou --nouveau-code)");
            return 1;
        }
        std::string out, summary;
        std::vector<ActivationRecord> records;
        if (!signLicense(*key, in, now, out, &why, &summary, &records)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        if (!a.one("--sortie").empty() && !writeText(a.one("--sortie"), out, why)) {
            complain("Refus\xC3\xA9 : " + why);
            return 1;
        }
        say("Sign\xC3\xA9" "e (en ligne) : " + summary + (key->test() ? "!!! sign\xC3\xA9" "e avec une CL\xC3\x89 D'ESSAI !!!\n" : ""));
        noteActivations(records);
        archiveFile(out, *key);
        const bool newCode = !a.flag("--etendre") || a.flag("--nouveau-code");
        std::string code, normalized, hash;
        if (newCode) {
            code = licensing::service::newActivationCode();
            (void)licensing::service::normalizeCode(code, normalized);
            hash = licensing::service::codeHash(normalized);
            crypto::wipe(normalized.data(), normalized.size());
        }
        const online::Result r = online::putLicence(folder, *key, out, hash, lease, true, o);
        if (!r.ok) {
            crypto::wipe(code.data(), code.size());
            complain(r.text() + "\nLa licence est sign\xC3\xA9" "e et rang\xC3\xA9" "e dans l'archive, mais PAS sur le service : " +
                     (newCode ? std::string("son code n'est pas montr\xC3\xA9 (il ne servirait \xC3\xA0 rien). ") : std::string()) +
                     "Relance plus tard : licence-en-ligne --parametres " + a.one("--parametres") + " --etendre --nouveau-code");
            return 1;
        }
        say(r.text() + "\n");
        if (newCode) {
            // 1.37.0 : garde chiffre sur ce poste (pour ce compte) : il se revoit (code-licence --licence ID)
            std::string keepWhy;
            const bool kept = online::storeCode(folder, id, code, &keepWhy);
            say("\n  CODE D'ACTIVATION :\n\n      " + code + "\n\n"
                "  Le client le tape dans Aide \xE2\x80\xBA Licence\xE2\x80\xA6 \xE2\x80\xBA Activer avec un code (ou dans l'installateur).\n" +
                (kept ? std::string("  Il est gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste (pour ce compte) : code-licence --licence ID pour le revoir.\n")
                      : "  ATTENTION : il n'a pas pu \xC3\xAAtre gard\xC3\xA9 sur ce poste (" + keepWhy + ") : note-le.\n") +
                "  Le service n'en garde que l'empreinte. Perdu : licence-en-ligne ... --etendre --nouveau-code (l'ancien ne marche plus).\n");
            crypto::wipe(code.data(), code.size());
        } else {
            say("Le code d'activation reste le m\xC3\xAAme.\n");
        }
        return 0;
    }
    // 1.37.0 : revoir le code d'activation d'une licence en ligne (garde chiffre sur ce poste)
    if (a.command == "code-licence") {
        if (!need("--licence")) return 2;
        std::string code, w;
        if (!online::readCode(folder, a.one("--licence"), code, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Code d'activation de " + a.one("--licence") + " : " + code + "\n");
        crypto::wipe(code.data(), code.size());
        return 0;
    }
    // 1.37.0 : le mail au client (le reglage, le mot de passe d'application, un essai)
    if (a.command == "mail-config") {
        if (!need("--hote") || !need("--expediteur")) return 2;
        license_admin::mail::Config cfg;
        cfg.host = a.one("--hote");
        cfg.security = a.one("--securite").empty() ? std::string("starttls") : a.one("--securite");
        cfg.port = a.one("--port").empty() ? license_admin::mail::defaultPort(cfg.security) : std::atoi(a.one("--port").c_str());
        cfg.from = a.one("--expediteur");
        cfg.fromName = a.one("--nom");
        cfg.user = a.one("--identifiant").empty() ? cfg.from : a.one("--identifiant");
        std::string w;
        if (!license_admin::mail::saveConfig(folder, cfg, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("Mail r\xC3\xA9gl\xC3\xA9 : " + cfg.from + " par " + cfg.host + ":" + std::to_string(cfg.port) + ".\n" +
            (license_admin::mail::hasPassword(folder) ? std::string("Essaie-le : mail-essai.\n") : std::string("Il manque le mot de passe d'application : mail-mot-de-passe.\n")));
        return 0;
    }
    if (a.command == "mail-mot-de-passe") {
        std::fputs("Mot de passe d'application (rien ne s'affiche) : ", stderr);
        std::string pw = readSecretLine();
        std::string w;
        const bool ok = !pw.empty() && license_admin::mail::savePassword(folder, pw, &w);
        crypto::wipe(pw.data(), pw.size());
        if (!ok) {
            complain("Refus\xC3\xA9 : " + (w.empty() ? std::string("aucun mot de passe tap\xC3\xA9") : w));
            return 1;
        }
        say("Mot de passe gard\xC3\xA9 chiffr\xC3\xA9 sur ce poste.\n");
        return 0;
    }
    if (a.command == "mail-essai") {
        license_admin::mail::Config cfg;
        std::string w;
        if (!license_admin::mail::loadConfig(folder, cfg, &w)) {
            complain("Refus\xC3\xA9 : " + w + " (mail-config d'abord)");
            return 1;
        }
        license_admin::mail::Message m;
        m.to = a.one("--a").empty() ? cfg.from : a.one("--a");
        m.subject = "Essai du mail de l'outil de l'\xC3\xA9" "diteur XPG";
        m.body = "Bonjour,\n\nCe mail d'essai est parti de ton outil de l'\xC3\xA9" "diteur (XPG-License-Admin) : le r\xC3\xA9glage marche.\n";
        const auto r = license_admin::mail::send(folder, m);
        if (!r.ok) {
            complain(r.message);
            return 1;
        }
        say(r.message + "\n");
        return 0;
    }
    if (a.command == "mail-oublier") {
        std::string w;
        if (!license_admin::mail::forget(folder, &w)) {
            complain("Refus\xC3\xA9 : " + w);
            return 1;
        }
        say("R\xC3\xA9glage du mail oubli\xC3\xA9 : aucun mail ne part plus.\n");
        return 0;
    }
    if (a.command == "kit-studios") {
        if (!need("--sortie")) return 2;
        auto key = keyOrFail();
        if (!key) return 1;
        return showResult(online::studioKit(folder, key.get(), a.one("--sortie"), licensing::systemNow()));
    }
    if (a.command.rfind("github-", 0) == 0 || a.command.rfind("passerelle-", 0) == 0) {
        complain("La commande " + a.command + " (1.31 \xC3\xA0 1.34) n'existe plus. L'archive se synchronise par le service des licences "
                 "(archive-cle, archive-synchroniser) et les demandes des clients y arrivent (service-demandes).\n\n" + usage());
        return 2;
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

