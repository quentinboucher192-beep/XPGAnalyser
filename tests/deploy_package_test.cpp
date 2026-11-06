// =============================================================================
//  tests/deploy_package_test.cpp - 1.28.0 : le paquet .xpgdeploy et la cle d'editeur
// -----------------------------------------------------------------------------
//  De vrais fichiers, dans un dossier temporaire : un projet IHM SYNTHETIQUE
//  (trois ressources reprises de tests/fixtures/ihm si le dossier est donne),
//  mis en FINISH, avec ce qui ne doit pas partir (versions, exports, corbeilles,
//  historique de marche, fichiers temporaires...). On construit, on verifie ;
//  puis chaque refus, sur des zips pieges fabriques ici avec miniz : un octet
//  change, un fichier en plus, en moins, la signature fausse, une autre cle
//  d'editeur, le manifeste modifie apres signature, les chemins dangereux, un
//  lien symbolique, les limites, un projet DEV ; la cle d'editeur : creee,
//  relue identique, un autre dossier donne une autre cle, une cle abimee n'est
//  jamais remplacee.
//
//  deploy_package_test [tests/fixtures/ihm]   (XPG_VERBOSE=1 : chaque raison)
// =============================================================================
#include "../src/deploy/Crypto.hpp"
#include "../src/deploy/DevKey.hpp"
#include "../src/deploy/Package.hpp"

#include "../third_party/miniz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
namespace c = deploy::crypto;
using deploy::PackageVerdict;

namespace {

int g_checks = 0, g_failures = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_verbose) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string U(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// ---- les zips de l'essai, avec miniz ----
using Entries = std::vector<std::pair<std::string, std::string>>;

Entries readZip(const fs::path& p) {
    Entries out;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, U(p).c_str(), 0)) return out;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[1024];
        mz_zip_reader_get_filename(&zip, i, name, sizeof name);
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        out.emplace_back(name, data ? std::string(static_cast<const char*>(data), size) : std::string());
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
}

bool writeZip(const fs::path& p, const Entries& entries) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, U(p).c_str(), 0)) return false;
    bool ok = true;
    for (const auto& [name, data] : entries)
        ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_DEFAULT_LEVEL);
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    return ok;
}

const std::string* entry(const Entries& e, const std::string& name) {
    for (const auto& [n, d] : e)
        if (n == name) return &d;
    return nullptr;
}

Entries without(Entries e, const std::string& name) {
    std::erase_if(e, [&](const auto& x) { return x.first == name; });
    return e;
}

Entries replaced(Entries e, const std::string& name, const std::string& data) {
    for (auto& [n, d] : e)
        if (n == name) d = data;
    return e;
}

// Les fichiers app/ d'un paquet, avec un manifeste neuf signe par `key`.
Entries signedWith(Entries appFiles, const deploy::PackageManifest& m, const deploy::DevKey& key) {
    appFiles = without(without(appFiles, "manifest.json"), "manifest.sig");
    const std::string json = deploy::writeManifest(m);
    appFiles.emplace_back("manifest.json", json);
    appFiles.emplace_back("manifest.sig", c::hex(key.sign(json)));
    return appFiles;
}

// Remplacer, dans le repertoire central du zip, ce qu'il dit d'une entree : l'hote (version made by) et les attributs.
bool patchCentral(const fs::path& p, const std::string& name, std::uint16_t madeBy, std::uint32_t attributes) {
    std::string bytes = readFile(p);
    for (std::size_t at = bytes.find("PK\x01\x02"); at != std::string::npos; at = bytes.find("PK\x01\x02", at + 4)) {
        if (at + 46 > bytes.size()) break;
        const auto u8 = [&](std::size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(bytes[at + k])); };
        const std::size_t len = u8(28) | (u8(29) << 8);
        if (bytes.compare(at + 46, len, name) != 0) continue;
        bytes[at + 4] = static_cast<char>(madeBy & 0xFF);
        bytes[at + 5] = static_cast<char>(madeBy >> 8);
        for (int k = 0; k < 4; ++k) bytes[at + 38 + static_cast<std::size_t>(k)] = static_cast<char>((attributes >> (8 * k)) & 0xFF);
        writeFile(p, bytes);
        return true;
    }
    return false;
}

// Remplacer un nom d'entree (meme longueur) dans les en-tetes locaux et le repertoire central.
bool renameInPlace(const fs::path& p, const std::string& from, const std::string& to) {
    std::string bytes = readFile(p);
    bool any = false;
    for (std::size_t at = bytes.find(from); at != std::string::npos; at = bytes.find(from, at + 1)) {
        bytes.replace(at, from.size(), to);
        any = true;
    }
    if (any) writeFile(p, bytes);
    return any;
}

deploy::PackageReport verify(const fs::path& p, std::vector<c::PublicKey> keys, bool inspect = false, deploy::PackageLimits limits = {}) {
    deploy::PackageVerifyOptions o;
    o.approvedKeys = std::move(keys);
    o.inspect = inspect;
    o.limits = limits;
    return deploy::verifyPackage(U(p), o);
}

void expectRefused(const deploy::PackageReport& r, int step, PackageVerdict verdict, const std::string& part, const std::string& label) {
    if (g_verbose) std::printf("         [%s] etape %d : %s\n", label.c_str(), r.failedStep, r.reason.c_str());
    check(r.verdict == verdict && r.failedStep == step && has(r.reason, part),
          label + " : etape " + std::to_string(r.failedStep) + " [" + r.reason + "]");
}

// ---- le projet synthetique ----
const char* kVersions = "# IHM Studio - les versions du projet.\n"
                        "reglages auto=jamais garder_auto=10\n"
                        "version n=1 nom=\"Premiere etat=livree\" etat=brouillon date=\"2026-10-01 10:00\"\n"
                        "version n=2 nom=\"Mise en service\" etat=validee date=\"2026-11-01 09:00\"\n"
                        "version n=3 nom=\"Essai\" etat=brouillon date=\"2026-11-02 09:00\"\n";

std::string projectManifest(const std::string& state, const std::string& edition = "ihm") {
    return "# API Studio project. Safe to read; edit with care.\nformatVersion = 1\nname = Essai_Deploiement\nversion = 0.0.1\nstate = " + state
           + "\ncreated = 2026-11-06 10:00\nmodified = 2026-11-06 10:00\nauthor = essai\ncomment = \n" + (edition.empty() ? "" : "edition = " + edition + "\n");
}

void makeProject(const fs::path& root, const fs::path& fixtures) {
    const auto fixture = [&](const char* name, const std::string& fallback) {
        const fs::path f = fixtures / name;
        std::error_code ec;
        return !fixtures.empty() && fs::is_regular_file(f, ec) ? readFile(f) : fallback;
    };
    writeFile(root / "project.xpgproj", projectManifest("FINISH"));
    writeFile(root / "ihm" / "ihm.txt", "# IHM Studio\nformat 33\nvue id=1 fichier=vues/0001-Accueil.vue\nfin\n");
    writeFile(root / "ihm" / "vues" / "0001-Accueil.vue", "vue id=1 nom=Accueil\nobjet id=2 genre=texte texte=\"Bonjour\"\nfin\n");
    writeFile(root / "ihm" / "scripts" / "0001-init.st", "x := 1;\n");
    writeFile(root / "ihm" / "ressources" / "0002-logo_site.png", fixture("logo_site.png", std::string(5000, '\x89') + "PNG-essai"));
    writeFile(root / "ihm" / "ressources" / "0003-pompe.svg", fixture("pompe.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\"/>"));
    writeFile(root / "ihm" / "vide.txt", "");
    writeFile(root / "donnees" / "consignes.csv", fixture("consignes.csv", "nom;valeur\nP1;3\n"));
    // Ce qui reste sur ce PC.
    writeFile(root / "ihm" / "corbeille" / "0009-Ancienne.vue", "vue\nfin\n");
    writeFile(root / "ihm" / "supprimes" / "corbeille.txt", "fin\n");
    writeFile(root / "ihm" / "historique" / "alarmes.csv", "alarme;date\n");
    writeFile(root / "ihm" / "historique-edition" / "0001.txt", "fin\n");
    writeFile(root / "ihm" / "reseau" / "decouvertes.csv", "ip\n");
    writeFile(root / "ihm" / "ressources" / "enregistrements" / "incident.xrec", "rec");
    writeFile(root / "ihm" / "ihm.txt.tmp", "coupe");
    writeFile(root / "ihm" / "vues" / "0001-Accueil.vue.bak", "ancienne");
    writeFile(root / "donnees" / "extraits" / "sortie.csv", "a;b\n");
    writeFile(root / "versions" / "index.txt", kVersions);
    writeFile(root / "exports" / "dossier.pdf", "%PDF-1.4");
    writeFile(root / ".xpg" / "build" / "build-cache.txt", "cache\n");
    writeFile(root / "sections" / "MAST.st", "(* programme *)\n");
    writeFile(root / "lancer-poste.sh", "#!/bin/sh\n");
    writeFile(root / "ancien.xpgdeploy", "PK");
    writeFile(root / "notes.txt", "a ne pas envoyer\n");
}

const char* kLauncher = "@echo off\r\nrem Poste d'exploitation - Essai_Deploiement (IHM Studio)\r\nchcp 65001 >nul\r\n"
                        "start \"\" \"C:\\Program Files\\API IHM Studio\\IHM-Studio.exe\" --ihm \"%~dp0.\"\r\n";

deploy::PackageBuildOptions buildOptions(const fs::path& project, const fs::path& out) {
    deploy::PackageBuildOptions o;
    o.projectFolder = U(project);
    o.outputPath = U(out);
    o.launcherName = "lancer-poste.cmd";
    o.launcherText = kLauncher;
    return o;
}

} // namespace

int main(int argc, char** argv) {
    g_verbose = std::getenv("XPG_VERBOSE") != nullptr;
    check(c::init(), "libsodium pret");
    const fs::path fixtures = argc > 1 ? fs::path(argv[1]) : fs::path();
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / ("xpg-deploy-package-" + c::randomDigits(8));
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    const fs::path project = base / "Essai_Deploiement";
    makeProject(project, fixtures);

    // ======================================================= la cle d'editeur ==
    std::string why;
    auto keyA = deploy::DevKey::loadOrCreate(U(base / "cleA"), &why, "Poste essai A");
    check(keyA != nullptr, "cle A creee [" + why + "]");
    if (!keyA) return 1;
    check(keyA->created() && keyA->name() == "Poste essai A", "cle A : neuve, son nom (" + keyA->name() + ")");
    check(keyA->publicKeyHex().size() == 64 && fs::exists(base / "cleA" / "cle_editeur.bin"), "cle A : 64 caracteres, cle_editeur.bin");
    check(has(keyA->fingerprint(), ":") && keyA->fingerprint() == deploy::keyFingerprint(keyA->publicKey()), "cle A : l'empreinte " + keyA->fingerprint());
#ifndef _WIN32
    {
        struct stat st {};
        check(::stat(U(base / "cleA" / "cle_editeur.bin").c_str(), &st) == 0 && (st.st_mode & 077) == 0, "cle A : fichier en 0600");
    }
#endif
    {
        const std::string onDisk = readFile(base / "cleA" / "cle_editeur.bin");
        check(has(onDisk, "publique=" + keyA->publicKeyHex()) && has(onDisk, "nom=Poste essai A"), "cle A : le fichier porte la cle publique et le nom");
        auto again = deploy::DevKey::loadOrCreate(U(base / "cleA"), &why);
        check(again && !again->created() && again->publicKey() == keyA->publicKey() && again->name() == "Poste essai A",
              "cle A relue : la meme, avec son nom [" + why + "]");
        if (again) {
            const auto s1 = keyA->sign("message"), s2 = again->sign("message");
            check(s1 == s2 && c::verify(keyA->publicKey(), "message", s2), "cle A relue : signe comme la premiere");
            check(again->rename("Poste essai A2", &why), "cle A : renommer [" + why + "]");
            auto third = deploy::DevKey::loadOrCreate(U(base / "cleA"), &why);
            check(third && third->name() == "Poste essai A2" && third->publicKey() == keyA->publicKey(), "cle A : le nom change, la paire reste");
        }
    }
    auto keyB = deploy::DevKey::loadOrCreate(U(base / "cleB"), &why);
    check(keyB && keyB->publicKey() != keyA->publicKey() && !keyB->name().empty(), "un autre dossier : une autre cle (" + (keyB ? keyB->name() : "") + ")");
    if (!keyB) return 1;
    {
        // Une cle abimee : refusee, jamais remplacee.
        writeFile(base / "cleC" / "cle_editeur.bin", "format=1\nnom=X\npublique=zz\n");
        std::string whyC;
        auto keyC = deploy::DevKey::loadOrCreate(U(base / "cleC"), &whyC);
        check(!keyC && has(whyC, "supprime ce fichier"), "une cle abimee est refusee [" + whyC + "]");
        check(readFile(base / "cleC" / "cle_editeur.bin") == "format=1\nnom=X\npublique=zz\n", "une cle abimee n'est pas remplacee");
        // La partie secrete d'une autre cle : refusee (la paire ne se tient pas).
        std::string mixed = readFile(base / "cleA" / "cle_editeur.bin");
        const std::string other = readFile(base / "cleB" / "cle_editeur.bin");
        mixed = mixed.substr(0, mixed.find("secrete=")) + other.substr(other.find("secrete="));
        writeFile(base / "cleD" / "cle_editeur.bin", mixed);
        auto keyD = deploy::DevKey::loadOrCreate(U(base / "cleD"), &whyC);
        check(!keyD && has(whyC, "ne correspond pas"), "une cle dont la partie secrete est d'une autre paire est refusee [" + whyC + "]");
    }

    // ============================================================ la collecte ==
    {
        const auto col = deploy::collectProject(U(project));
        check(col.ok(), "collecte : le projet FINISH peut partir [" + col.refusal + "]");
        std::vector<std::string> got;
        for (const auto& f : col.files) got.push_back(f.relative);
        const std::vector<std::string> want = {"donnees/consignes.csv", "ihm/ihm.txt", "ihm/ressources/0002-logo_site.png", "ihm/ressources/0003-pompe.svg",
                                               "ihm/scripts/0001-init.st", "ihm/vide.txt", "ihm/vues/0001-Accueil.vue", "project.xpgproj"};
        std::string list;
        for (const auto& g : got) list += g + " ";
        check(got == want, "collecte : exactement les fichiers du poste (" + list + ")");
        std::string skipped;
        for (const auto& s : col.skipped) skipped += s.what + " ";
        for (const char* w : {"versions/", "exports/", ".xpg/", "sections/", "ihm/corbeille/", "ihm/supprimes/", "ihm/historique/", "ihm/historique-edition/",
                              "ihm/reseau/", "ihm/ressources/enregistrements/", "ihm/ihm.txt.tmp", "ihm/vues/0001-Accueil.vue.bak", "donnees/extraits/",
                              "lancer-poste.sh", "ancien.xpgdeploy", "notes.txt"})
            check(has(" " + skipped, std::string(" ") + w), std::string("collecte : laisse ") + w);
        check(col.project.name == "Essai_Deploiement" && col.project.state == "FINISH" && col.project.lastVersion == "V2",
              "collecte : nom, etat, derniere version validee (" + col.project.lastVersion + ")");
    }

    // ============================================================= construire ==
    const fs::path out = base / "sortie" / "Essai_Deploiement-V2.xpgdeploy";
    deploy::PackageBuildResult built;
    {
        auto o = buildOptions(project, out);
        std::size_t calls = 0;
        std::uint64_t lastDone = 0, total = 0;
        o.progress = [&](const deploy::PackageProgress& p) {
            ++calls;
            lastDone = p.bytesDone;
            total = p.bytesTotal;
            return true;
        };
        built = deploy::buildPackage(o, *keyA);
        check(built.ok, "construire [" + built.error + "]");
        check(calls > 8 && lastDone == total && total > 0, "construire : la progression va au bout (" + std::to_string(calls) + " appels)");
        std::string hexDigest;
        std::uint64_t size = 0;
        check(c::sha256File(U(out), hexDigest, &size, nullptr) && hexDigest == built.sha256 && size == built.packageBytes,
              "construire : le SHA-256 et la taille du paquet sont ceux du fichier");
        check(!fs::exists(fs::path(out).concat(".tmp")), "construire : pas de fichier temporaire laisse");
        const auto& m = built.manifest;
        check(m.application == "Essai_Deploiement" && m.appVersion == "V2" && m.launcher == "app/lancer-poste.cmd" && m.publisherKey == keyA->publicKeyHex()
                  && m.publisherName == keyA->name() && m.runtimeProgram == "IHM-Studio.exe" && m.runtimeMode == "--ihm" && !m.runtimeMinVersion.empty()
                  && m.files.size() == 9 && m.createdAt.size() == 20 && m.createdAt.back() == 'Z',
              "construire : le manifeste (" + m.application + " " + m.appVersion + ", " + std::to_string(m.files.size()) + " fichiers, " + m.createdAt + ")");
        const Entries zip = readZip(out);
        const std::string* json = entry(zip, "manifest.json");
        const std::string* sig = entry(zip, "manifest.sig");
        check(json && sig && sig->size() == 128, "le zip : manifest.json, manifest.sig (128 caracteres)");
        if (json && sig) {
            c::Bytes s;
            c::Signature sg{};
            if (c::unhex(*sig, s, 64)) std::copy(s.begin(), s.end(), sg.begin());
            check(c::verify(keyA->publicKey(), *json, sg), "le zip : la signature est celle des octets de manifest.json");
            check(*json == deploy::writeManifest(m), "le zip : manifest.json est le manifeste annonce, octet pour octet");
            deploy::PackageManifest back;
            check(deploy::parseManifest(*json, back, &why) && back.files.size() == m.files.size() && back.totalSize == m.totalSize
                      && deploy::writeManifest(back) == *json,
                  "relire le manifeste, le reecrire : les memes octets [" + why + "]");
            check(has(*json, "\"format\": \"xpgdeploy\"") && has(*json, "\"runtime\"") && has(*json, "\"publisher\"") && has(*json, "\"totalSize\""),
                  "le manifeste : le format de DEPLOIEMENT.md");
        }
        check(entry(zip, "app/lancer-poste.cmd") && *entry(zip, "app/lancer-poste.cmd") == kLauncher, "le zip : le lanceur, tel que donne");
        check(entry(zip, "app/ihm/ihm.txt") && entry(zip, "app/project.xpgproj") && entry(zip, "app/ihm/vide.txt") && !entry(zip, "app/notes.txt")
                  && !entry(zip, "app/versions/index.txt") && !entry(zip, "app/ihm/historique/alarmes.csv"),
              "le zip : les fichiers du poste sous app/, rien d'autre");
        check(zip.size() == 11, "le zip : 9 fichiers + manifeste + signature (" + std::to_string(zip.size()) + ")");
    }

    // ============================================================== verifier ==
    {
        const auto ok = verify(out, {keyA->publicKey()});
        check(ok.ok() && ok.failedStep == 0 && ok.signatureValid && ok.publisherApproved && ok.manifestRead,
              "verifier : accepte avec la cle A approuvee [" + ok.reason + "]");
        check(ok.publisherFingerprint == keyA->fingerprint(), "verifier : l'empreinte de l'editeur");
        const auto two = verify(out, {keyB->publicKey(), keyA->publicKey()});
        check(two.ok(), "verifier : la bonne cle parmi plusieurs");
        const auto none = verify(out, {});
        expectRefused(none, 2, PackageVerdict::UnapprovedPublisher, "approuv", "aucune cle approuvee : signature valide, editeur non approuve");
        check(none.signatureValid && !none.publisherApproved, "aucune cle approuvee : la signature est dite valide");
        const auto look = verify(out, {}, /*inspect=*/true);
        check(look.verdict == PackageVerdict::UnapprovedPublisher && look.failedStep == 0 && look.signatureValid && has(look.reason, "intact"),
              "inspecter : signature valide, editeur non approuve, contenu intact [" + look.reason + "]");
        // Annuler la verification.
        deploy::PackageVerifyOptions vo;
        vo.approvedKeys = {keyA->publicKey()};
        vo.progress = [](const deploy::PackageProgress& p) { return p.filesDone < 2; };
        const auto cut = deploy::verifyPackage(U(out), vo);
        check(!cut.ok() && cut.cancelled && cut.failedStep == 4, "verifier : annulable [" + cut.reason + "]");
    }

    // Une autre cle d'editeur, non approuvee.
    {
        const fs::path outB = base / "sortie" / "par-B.xpgdeploy";
        const auto rb = deploy::buildPackage(buildOptions(project, outB), *keyB);
        check(rb.ok, "construire avec la cle B [" + rb.error + "]");
        const auto r = verify(outB, {keyA->publicKey()});
        expectRefused(r, 2, PackageVerdict::UnapprovedPublisher, keyB->fingerprint(), "une autre cle d'editeur : non approuvee (pas une signature fausse)");
        check(r.signatureValid, "une autre cle d'editeur : sa signature est valide");
    }

    // ===================================================== les paquets pieges ==
    const Entries good = readZip(out);
    deploy::PackageManifest gm;
    check(entry(good, "manifest.json") && deploy::parseManifest(*entry(good, "manifest.json"), gm, &why), "le manifeste du bon paquet");
    const auto trap = [&](const std::string& name, const Entries& e) {
        const fs::path p = base / "pieges" / (name + ".xpgdeploy");
        fs::create_directories(p.parent_path());
        check(writeZip(p, e), "fabriquer " + name);
        return p;
    };
    const std::vector<c::PublicKey> A = {keyA->publicKey()};
    {
        std::string ihm = *entry(good, "app/ihm/ihm.txt");
        ihm[3] = static_cast<char>(ihm[3] ^ 1);
        expectRefused(verify(trap("octet", replaced(good, "app/ihm/ihm.txt", ihm)), A), 4, PackageVerdict::Refused, "SHA-256", "un octet change dans un fichier");
    }
    {
        // L'octet change dans le paquet lui-meme (la ressource PNG, stockee telle quelle) : le zip est abime.
        std::string bytes = readFile(out);
        const std::string png = *entry(good, "app/ihm/ressources/0002-logo_site.png");
        const std::string probe = png.substr(png.size() / 2, 24);
        const auto at = bytes.find(probe);
        check(at != std::string::npos, "le PNG est stocke tel quel dans le paquet");
        if (at != std::string::npos) {
            bytes[at + 5] = static_cast<char>(bytes[at + 5] ^ 0x40);
            const fs::path p = base / "pieges" / "abime.xpgdeploy";
            writeFile(p, bytes);
            expectRefused(verify(p, A), 4, PackageVerdict::Refused, "0002-logo_site.png", "un octet change dans le paquet (CRC)");
        }
    }
    {
        Entries plus = good;
        plus.emplace_back("app/intrus.txt", "je n'ai rien a faire ici");
        expectRefused(verify(trap("plus", plus), A), 4, PackageVerdict::Refused, "ne cite pas", "un fichier en plus dans le zip");
        expectRefused(verify(trap("moins", without(good, "app/ihm/vues/0001-Accueil.vue")), A), 4, PackageVerdict::Refused, "manque",
                      "un fichier en moins dans le zip");
    }
    {
        std::string sig = *entry(good, "manifest.sig");
        sig[10] = sig[10] == '0' ? '1' : '0';
        expectRefused(verify(trap("signature", replaced(good, "manifest.sig", sig)), A), 2, PackageVerdict::BadSignature, "fausse", "la signature fausse");
        expectRefused(verify(trap("signature-pas-hex", replaced(good, "manifest.sig", std::string(128, 'z'))), A), 2, PackageVerdict::BadSignature,
                      "128", "la signature n'est pas de l'hexadecimal");
        std::string json = *entry(good, "manifest.json");
        const auto at = json.find("\"V2\"");
        json.replace(at, 4, "\"V9\"");
        const auto r = verify(trap("manifeste-modifie", replaced(good, "manifest.json", json)), A);
        expectRefused(r, 2, PackageVerdict::BadSignature, "modifi", "le manifeste modifie apres signature");
        check(!r.signatureValid, "le manifeste modifie : la signature n'est pas dite valide");
        expectRefused(verify(trap("sans-signature", without(good, "manifest.sig")), A), 1, PackageVerdict::Refused, "manifest.sig", "sans manifest.sig");
        expectRefused(verify(trap("sans-manifeste", without(good, "manifest.json")), A), 1, PackageVerdict::Refused, "manifest.json", "sans manifest.json");
        writeFile(base / "pieges" / "texte.xpgdeploy", "ceci n'est pas un zip");
        expectRefused(verify(base / "pieges" / "texte.xpgdeploy", A), 1, PackageVerdict::Refused, "zip", "un fichier qui n'est pas un zip");
        expectRefused(verify(base / "pieges" / "absent.xpgdeploy", A), 1, PackageVerdict::Refused, "introuvable", "un paquet absent");
    }
    {
        // Signe par A, mais annonce la cle de B.
        auto m = gm;
        m.publisherKey = keyB->publicKeyHex();
        const fs::path p = trap("autre-editeur-annonce", signedWith(good, m, *keyA));
        expectRefused(verify(p, A), 2, PackageVerdict::Refused, "autre", "signe par A, annonce B");
        expectRefused(verify(p, {}), 2, PackageVerdict::BadSignature, "fausse", "signe par A, annonce B, sans cle approuvee");
        // Un format plus recent ; une cle en double ; un manifeste qui n'est pas du JSON (signes).
        m = gm;
        m.version = 2;
        expectRefused(verify(trap("version-2", signedWith(good, m, *keyA)), A), 2, PackageVerdict::Refused, "plus r", "un format plus recent");
        Entries dup = without(without(good, "manifest.json"), "manifest.sig");
        std::string json = deploy::writeManifest(gm);
        json.insert(1, "\n  \"format\": \"xpgdeploy\",");
        dup.emplace_back("manifest.json", json);
        dup.emplace_back("manifest.sig", c::hex(keyA->sign(json)));
        expectRefused(verify(trap("cle-double", dup), A), 2, PackageVerdict::Refused, "deux fois", "une cle en double dans le manifeste");
        dup = replaced(replaced(dup, "manifest.json", "{pas du json"), "manifest.sig", c::hex(keyA->sign("{pas du json")));
        expectRefused(verify(trap("pas-json", dup), A), 2, PackageVerdict::Refused, "illisible", "un manifeste qui n'est pas du JSON");
    }
    {
        // Le manifeste signe ment sur une taille, le total, le lanceur.
        auto m = gm;
        for (auto& f : m.files)
            if (f.path == "app/ihm/ihm.txt") f.size += 1;
        m.totalSize += 1;
        expectRefused(verify(trap("taille", signedWith(good, m, *keyA)), A), 4, PackageVerdict::Refused, "taille", "une taille fausse (signee)");
        m = gm;
        m.totalSize += 7;
        expectRefused(verify(trap("total", signedWith(good, m, *keyA)), A), 4, PackageVerdict::Refused, "totale", "un total faux (signe)");
        m = gm;
        m.launcher = "app/autre.cmd";
        expectRefused(verify(trap("lanceur", signedWith(good, m, *keyA)), A), 4, PackageVerdict::Refused, "lanceur", "un lanceur absent de la liste");
        m = gm;
        for (auto& f : m.files)
            if (f.path == "app/ihm/vide.txt") f.sha256 = std::string(64, '0');
        expectRefused(verify(trap("empreinte", signedWith(good, m, *keyA)), A), 4, PackageVerdict::Refused, "SHA-256", "une empreinte fausse (signee)");
    }

    // ===================================================== les chemins dangereux ==
    {
        struct Case {
            std::string path, part, label;
            bool inZip;
        };
        const std::vector<Case> cases = {
            {"../evil.txt", "sortirait", "../ en tete", true},
            {"app/../../evil.txt", "sortirait", "app/../..", true},
            {"/abs/evil.txt", "absolu", "un chemin absolu /abs", false},
            {"C:\\Windows\\evil.txt", "\\", "C:\\", true},
            {"app/C:evil.txt", ":", "une lettre de lecteur sous app/", true},
            {"\\\\serveur\\partage\\evil.txt", "absolu", "\\\\serveur", true},
            {"app/CON", "CON", "CON", true},
            {"app/ihm/nul.txt", "nul", "nul.txt", true},
            {"app/COM1", "COM1", "COM1", true},
            {"app/lpt9.log", "lpt9", "lpt9.log", true},
            {"app/ihm/fin.", "point", "un point final", true},
            {"app/ihm/fin ", "espace", "une espace finale", true},
            {"app/" + std::string(240, 'a'), "240", "trop long", true},
            {"app/a\x01" "b.txt", "contr", "un caractere de controle", true},
            {"app/a*b.txt", "*", "un caractere interdit par Windows", true},
            {"app//double.txt", "vide", "un segment vide", true},
            {"ailleurs/x.txt", "app/", "hors de app/", true},
            {"app/\xC3\x28.txt", "UTF-8", "de l'UTF-8 invalide", true},
        };
        for (const auto& k : cases) {
            auto m = gm;
            const std::string data = "contenu piege";
            m.files.push_back({k.path, data.size(), c::sha256Hex(data)});
            m.totalSize += data.size();
            Entries e = good;
            if (k.inZip) e.emplace_back(k.path, data);
            const auto p = trap("chemin-" + std::to_string(&k - cases.data()), signedWith(e, m, *keyA));
            expectRefused(verify(p, A), 3, PackageVerdict::Refused, k.part, "chemin dangereux (manifeste) : " + k.label);
            check(!deploy::safePackagePath(k.path), "safePackagePath refuse : " + k.label);
        }
        check(deploy::safePackagePath("app/ihm/vues/0001-Accueil.vue") && deploy::safePackagePath("app/ihm/r\xC3\xA9sum\xC3\xA9.txt")
                  && deploy::safePackagePath("app/CONSOLE.txt") && deploy::safePackagePath("app/.cache/x"),
              "safePackagePath accepte les chemins ordinaires (accents, CONSOLE, .cache)");
        // Le chemin dangereux seulement dans le zip (le manifeste ne le cite pas) : refuse a l'etape 3, avant l'etape 4.
        Entries e = good;
        e.emplace_back("app/../evil.txt", "x");
        expectRefused(verify(trap("zip-seul", e), A), 3, PackageVerdict::Refused, "sortirait", "chemin dangereux dans le zip seul");
        // "/abs" : miniz refuse de l'ecrire ; on l'ecrit "Xabs" puis on le renomme dans les octets.
        e = good;
        e.emplace_back("Xabs/evil.txt", "x");
        const auto pAbs = trap("zip-absolu", e);
        check(renameInPlace(pAbs, "Xabs/evil.txt", "/abs/evil.txt"), "renommer l'entree en /abs");
        expectRefused(verify(pAbs, A), 3, PackageVerdict::Refused, "absolu", "chemin absolu dans le zip seul");
        // Deux chemins qui ne different que par la casse.
        auto m = gm;
        const std::string upper = "app/IHM/ihm.txt";
        m.files.push_back({upper, entry(good, "app/ihm/ihm.txt")->size(), c::sha256Hex(*entry(good, "app/ihm/ihm.txt"))});
        m.totalSize += entry(good, "app/ihm/ihm.txt")->size();
        e = good;
        e.emplace_back(upper, *entry(good, "app/ihm/ihm.txt"));
        expectRefused(verify(trap("casse", signedWith(e, m, *keyA)), A), 3, PackageVerdict::Refused, "casse", "deux chemins a la casse pres");
        // Un lien symbolique (attributs Unix 0120777) ; un dossier.
        e = good;
        e.emplace_back("app/lien", "/etc/passwd");
        m = gm;
        m.files.push_back({"app/lien", 11, c::sha256Hex("/etc/passwd")});
        m.totalSize += 11;
        const auto pLink = trap("lien", signedWith(e, m, *keyA));
        check(patchCentral(pLink, "app/lien", 0x0314, 0120777u << 16), "marquer l'entree comme lien symbolique");
        expectRefused(verify(pLink, A), 3, PackageVerdict::Refused, "lien symbolique", "un lien symbolique");
        e = good;
        e.emplace_back("app/dossier/", "");
        expectRefused(verify(trap("dossier", e), A), 3, PackageVerdict::Refused, "dossier", "une entree de dossier");
        // Deux entrees du meme nom.
        e = good;
        e.emplace_back("app/ihm/ihm.txt", "un autre contenu");
        expectRefused(verify(trap("doublon", e), A), 1, PackageVerdict::Refused, "m\xC3\xAAme nom", "deux entrees du meme nom");
    }

    // ============================================================== les limites ==
    {
        deploy::PackageLimits few;
        few.maxFiles = 5;
        expectRefused(verify(out, A, false, few), 1, PackageVerdict::Refused, "limite", "trop de fichiers (verifier)");
        deploy::PackageLimits small;
        small.maxFileBytes = 40;
        expectRefused(verify(out, A, false, small), 1, PackageVerdict::Refused, "par fichier", "un fichier trop gros (verifier)");
        deploy::PackageLimits light;
        light.maxTotalBytes = 100;
        expectRefused(verify(out, A, false, light), 1, PackageVerdict::Refused, "au total", "trop gros au total (verifier)");
        deploy::PackageLimits tiny;
        tiny.maxManifestBytes = 64;
        expectRefused(verify(out, A, false, tiny), 1, PackageVerdict::Refused, "manifest.json", "un manifeste trop gros (verifier)");
        deploy::PackageLimits shortPaths;
        shortPaths.maxPathLength = 20;
        expectRefused(verify(out, A, false, shortPaths), 3, PackageVerdict::Refused, "20", "des chemins trop longs pour la limite (verifier)");

        auto o = buildOptions(project, base / "sortie" / "limite.xpgdeploy");
        o.limits = few;
        auto r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, "au plus") && !fs::exists(base / "sortie" / "limite.xpgdeploy"), "trop de fichiers (construire) [" + r.error + "]");
        o.limits = small;
        r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, "par fichier"), "un fichier trop gros (construire) [" + r.error + "]");
        o.limits = light;
        r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, "au total"), "trop gros au total (construire) [" + r.error + "]");
    }

    // ============================================ les refus a la construction ==
    {
        const fs::path dev = base / "Projet_DEV";
        fs::copy(project, dev, fs::copy_options::recursive, ec);
        writeFile(dev / "project.xpgproj", projectManifest("DEV"));
        auto r = deploy::buildPackage(buildOptions(dev, base / "sortie" / "dev.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "FINISH") && has(r.error, "DEV") && !fs::exists(base / "sortie" / "dev.xpgdeploy"), "un projet DEV est refuse [" + r.error + "]");
        writeFile(dev / "project.xpgproj", projectManifest("LOCK"));
        r = deploy::buildPackage(buildOptions(dev, base / "sortie" / "dev.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "LOCK"), "un projet LOCK est refuse [" + r.error + "]");
        writeFile(dev / "project.xpgproj", projectManifest("FINISH", "api"));
        r = deploy::buildPackage(buildOptions(dev, base / "sortie" / "dev.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "API Studio"), "un projet d'API Studio est refuse [" + r.error + "]");
        writeFile(dev / "project.xpgproj", projectManifest("FINISH", ""));
        r = deploy::buildPackage(buildOptions(dev, base / "sortie" / "dev.xpgdeploy"), *keyA);
        check(r.ok, "un projet de la 1.11 (sans edition) FINISH part [" + r.error + "]");
        r = deploy::buildPackage(buildOptions(base / "nulle-part", base / "sortie" / "x.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "introuvable"), "un dossier absent [" + r.error + "]");
        r = deploy::buildPackage(buildOptions(base / "cleA", base / "sortie" / "x.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "project.xpgproj"), "un dossier qui n'est pas un projet [" + r.error + "]");
        fs::remove(dev / "ihm" / "ihm.txt", ec);
        r = deploy::buildPackage(buildOptions(dev, base / "sortie" / "x.xpgdeploy"), *keyA);
        check(!r.ok && has(r.error, "ihm.txt"), "un projet sans IHM [" + r.error + "]");
    }
    {
        // Des noms que Windows refuse, deux noms a la casse pres : refuses a la collecte.
        const fs::path bad = base / "Projet_noms";
        fs::copy(project, bad, fs::copy_options::recursive, ec);
#ifndef _WIN32
        writeFile(bad / "ihm" / "ressources" / "aux.png", "x");
        auto col = deploy::collectProject(U(bad));
        check(!col.ok() && has(col.refusal, "aux.png") && has(col.refusal, "Renomme"), "un fichier nomme aux.png est refuse [" + col.refusal + "]");
        fs::remove(bad / "ihm" / "ressources" / "aux.png", ec);
        writeFile(bad / "ihm" / "ressources" / "a:b.png", "x");
        col = deploy::collectProject(U(bad));
        check(!col.ok() && has(col.refusal, ":"), "un nom avec : est refuse [" + col.refusal + "]");
        fs::remove(bad / "ihm" / "ressources" / "a:b.png", ec);
        writeFile(bad / "ihm" / "ressources" / "Logo.PNG", "x");
        writeFile(bad / "ihm" / "ressources" / "logo.png", "y");
        col = deploy::collectProject(U(bad));
        check(!col.ok() && has(col.refusal, "casse"), "deux noms a la casse pres sont refuses [" + col.refusal + "]");
        fs::remove(bad / "ihm" / "ressources" / "Logo.PNG", ec);
        fs::remove(bad / "ihm" / "ressources" / "logo.png", ec);
        // Un lien symbolique dans le projet : jamais suivi.
        fs::create_symlink(project / "notes.txt", bad / "ihm" / "lien.txt", ec);
        col = deploy::collectProject(U(bad));
        bool linkTaken = false, linkLeft = false;
        for (const auto& f : col.files) linkTaken = linkTaken || f.relative == "ihm/lien.txt";
        for (const auto& s : col.skipped) linkLeft = linkLeft || (s.what == "ihm/lien.txt" && has(s.why, "lien"));
        check(!ec && col.ok() && !linkTaken && linkLeft, "un lien symbolique du projet n'est pas suivi");
#endif
    }
    {
        // Les options : le lanceur, l'extension ; l'annulation ; un fichier qui change pendant la construction.
        auto o = buildOptions(project, base / "sortie" / "x.xpgdeploy");
        o.launcherText.clear();
        auto r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, "lanceur"), "sans lanceur : refuse [" + r.error + "]");
        o = buildOptions(project, base / "sortie" / "x.zip");
        r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, ".xpgdeploy"), "une autre extension : refusee [" + r.error + "]");
        o = buildOptions(project, base / "sortie" / "annule.xpgdeploy");
        o.progress = [](const deploy::PackageProgress& p) { return p.filesDone < 3; };
        r = deploy::buildPackage(o, *keyA);
        check(!r.ok && r.cancelled && !fs::exists(base / "sortie" / "annule.xpgdeploy") && !fs::exists(base / "sortie" / "annule.xpgdeploy.tmp"),
              "annuler : rien n'est ecrit [" + r.error + "]");
        const fs::path moving = base / "Projet_change";
        fs::copy(project, moving, fs::copy_options::recursive, ec);
        o = buildOptions(moving, base / "sortie" / "change.xpgdeploy");
        o.progress = [&](const deploy::PackageProgress& p) {
            if (p.current == "app/ihm/scripts/0001-init.st") {
                std::ofstream more(moving / "ihm" / "scripts" / "0001-init.st", std::ios::binary | std::ios::app);
                more << "y := 2; (* ajoute pendant la construction *)\n";
            }
            return true;
        };
        r = deploy::buildPackage(o, *keyA);
        check(!r.ok && has(r.error, "0001-init.st") && !fs::exists(base / "sortie" / "change.xpgdeploy"),
              "un fichier qui change pendant la construction [" + r.error + "]");
        // Remplacer un paquet existant.
        r = deploy::buildPackage(buildOptions(project, out), *keyA);
        check(r.ok && verify(out, A).ok(), "reconstruire par-dessus un paquet existant [" + r.error + "]");
    }
    check(deploy::packageSizeText(2ull * 1024 * 1024 * 1024) == "2 Gio" && deploy::packageSizeText(1536) == "1,5 Kio" && deploy::packageSizeText(12) == "12 octets",
          "les tailles en texte (" + deploy::packageSizeText(1536) + ")");

    keyA.reset();
    keyB.reset();
    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
