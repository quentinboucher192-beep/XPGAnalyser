// =============================================================================
//  tests/admins_fichiers_test.cpp - 1.44.0 : LES FICHIERS, LES MESSAGES A UN PC,
//  LES VERSIONS, LES ADRESSES DE SECOURS ET LA SAUVEGARDE COMPLETE dans l'outil
//  de l'editeur (license_admin/gui/WorkspaceVersions.cpp), contre le VRAI Worker
//  lance sur ce poste (tests/service_local.hpp) - et un SECOND Worker neuf pour la
//  restauration
// -----------------------------------------------------------------------------
//  Sans reseau : la version d'un installateur lue dans sa ressource VS_VERSIONINFO
//  (de petits .exe PE fabriques ici : ProductVersion, FileVersion, les nombres
//  seuls ; un fichier qui n'est pas un .exe ; un .exe sans version).
//  Le service : un fichier de 3,5 Mio dont l'envoi est coupe puis repris (seuls
//  les morceaux qui manquent repartent), son lien (une fois : le second refuse),
//  le fichier entier par /v1/fichiers/lien ; un message a un PC connu (scelle
//  pour lui : il l'ouvre, un autre PC ne le recoit ni ne l'ouvre) ; une version
//  publiee (deux installateurs fabriques, de la meme version), refusee si elle
//  n'est pas plus recente ou si les deux different ; l'installateur de l'Admin
//  dans les notifications du Studio d'un PC (l'action << admin >>) et par un lien
//  d'une fois ; les adresses de secours (un registre numero + 1) ; la sauvegarde
//  (reglee par une phrase, faite, decrite, refusee avec une autre phrase, les 30
//  dernieres), puis restauree dans un dossier neuf ET un second service neuf : les
//  memes licences, la meme liste des admins, le meme journal (exportes des deux
//  services et compares), la cle de l'editeur remise signe et le second service
//  l'accepte ; les fichiers a renvoyer, renvoyes ; un service pas vide (409) ; une
//  autre cle de l'editeur deja la (refusee, puis mise de cote).
//  Aucune vraie cle, aucun vrai client : des cles creees ici, des noms d'essai.
//
//    admins_fichiers_test SOURCE
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
//  XPG_ESSAI_EXE=<un .exe> : sa version lue est aussi montree (un vrai installateur Inno).
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ArchiveSync.hpp"
#include "../src/license_admin/KeyBackup.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/licensing/Store.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = licensing::admins;
namespace sv = licensing::service;
namespace json = core::json;
namespace la = license_admin;
namespace on = license_admin::online;
namespace gui = license_admin::gui;

namespace {

int g_checks = 0, g_failures = 0;
void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    }
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
std::string randomBytes(std::size_t n, std::mt19937_64& rng) {
    std::string s(n, '\0');
    for (auto& c : s) c = static_cast<char>(rng() & 0xFF);
    return s;
}

struct Reply {
    int         status{0};
    std::string body;
    json::Value value;
};

// Un appel au service (signe par `key` si donnee, a l'heure du service).
Reply call(const std::string& base, const std::string& method, const std::string& path, const std::string& body, const lic::SigningKey* key) {
    lic::Seconds now = lic::systemNow();
    if (key) {
        auto c = app::ai::makeHttpClient();
        app::ai::HttpRequest q;
        q.method = "GET";
        q.url = base + "/v1/etat";
        std::string b;
        app::ai::HttpSink s;
        s.onData = [&b](std::string_view x) { b.append(x); };
        (void)c->perform(q, s);
        json::Value v;
        if (json::parse(b, v, nullptr) && v.isObject()) now = static_cast<lic::Seconds>(v.integer("heure", now));
    }
    auto client = app::ai::makeHttpClient();
    app::ai::HttpRequest req;
    req.method = method;
    req.url = base + path;
    req.body = body;
    req.connectTimeoutSeconds = 10;
    req.idleTimeoutSeconds = 30;
    if (!body.empty()) req.headers.emplace_back("Content-Type", "application/json");
    if (key)
        for (auto& h : sv::signRequest(method, path, body, key->publicKey(), [key](std::string_view m) { return key->sign(m); }, now)) req.headers.push_back(h);
    Reply out;
    app::ai::HttpSink sink;
    sink.onData = [&out](std::string_view chunk) { out.body.append(chunk.data(), chunk.size()); };
    const auto r = client->perform(req, sink);
    out.status = r.status;
    (void)json::parse(out.body, out.value, nullptr);
    return out;
}

// ---- un petit .exe (PE32) avec une ressource VS_VERSIONINFO (comme celle qu'ecrit Inno Setup) ----
void put16(std::string& b, std::size_t o, std::uint32_t v) {
    b[o] = static_cast<char>(v & 0xFF);
    b[o + 1] = static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& b, std::size_t o, std::uint32_t v) {
    put16(b, o, v & 0xFFFF);
    put16(b, o + 2, v >> 16);
}
std::string u16(const std::string& ascii) {
    std::string out;
    for (char c : ascii) {
        out += c;
        out += '\0';
    }
    out += std::string(2, '\0');
    return out;
}
void pad4(std::string& b) {
    while (b.size() % 4) b += '\0';
}
std::string verNode(const std::string& key, const std::string& value, std::uint16_t valueLen, std::uint16_t type, const std::vector<std::string>& children) {
    std::string b(6, '\0');
    b += u16(key);
    pad4(b);
    b += value;
    for (const auto& ch : children) {
        pad4(b);
        b += ch;
    }
    put16(b, 0, static_cast<std::uint32_t>(b.size()));
    put16(b, 2, valueLen);
    put16(b, 4, type);
    return b;
}
std::string strNode(const std::string& key, const std::string& value) {
    return verNode(key, u16(value), static_cast<std::uint16_t>(value.size() + 1), 1, {});
}
// `fixed` : {a, b, c, d} (FileVersion et ProductVersion) ; `product`, `file` : les chaines (vides : absentes) ; `withVersion`
// faux : aucune ressource ; `overlay` : des octets apres (les donnees d'un installateur).
std::string buildPe(const std::array<std::uint16_t, 4>& fixed, const std::string& product, const std::string& file, bool withVersion, const std::string& overlay) {
    std::string fixedInfo(52, '\0');
    put32(fixedInfo, 0, 0xFEEF04BDu);
    put32(fixedInfo, 4, 0x00010000u);
    put32(fixedInfo, 8, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 12, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 16, (static_cast<std::uint32_t>(fixed[0]) << 16) | fixed[1]);
    put32(fixedInfo, 20, (static_cast<std::uint32_t>(fixed[2]) << 16) | fixed[3]);
    put32(fixedInfo, 24, 0x3F);
    put32(fixedInfo, 32, 0x40004);
    put32(fixedInfo, 36, 1);
    std::vector<std::string> strings;
    strings.push_back(strNode("CompanyName", "Editeur d'essai"));
    if (!file.empty()) strings.push_back(strNode("FileVersion", file));
    strings.push_back(strNode("ProductName", "Installateur d'essai"));
    if (!product.empty()) strings.push_back(strNode("ProductVersion", product));
    std::vector<std::string> children;
    if (!product.empty() || !file.empty()) children.push_back(verNode("StringFileInfo", "", 0, 1, {verNode("040904b0", "", 0, 1, strings)}));
    std::string translation(4, '\0');
    put16(translation, 0, 0x0409);
    put16(translation, 2, 0x04b0);
    children.push_back(verNode("VarFileInfo", "", 0, 1, {verNode("Translation", translation, 4, 0, {})}));
    const std::string vi = verNode("VS_VERSION_INFO", fixedInfo, 52, 0, children);

    std::string rsrc(0x58, '\0');
    put16(rsrc, 0x0E, 1);                       // racine : une entree (type)
    put32(rsrc, 0x10, 16);                      // RT_VERSION
    put32(rsrc, 0x14, 0x80000000u | 0x18);
    put16(rsrc, 0x18 + 0x0E, 1);                // le type : une entree (nom 1)
    put32(rsrc, 0x28, 1);
    put32(rsrc, 0x2C, 0x80000000u | 0x30);
    put16(rsrc, 0x30 + 0x0E, 1);                // le nom : une entree (langue)
    put32(rsrc, 0x40, 0x409);
    put32(rsrc, 0x44, 0x48);
    put32(rsrc, 0x48, 0x1000 + 0x58);           // la donnee (RVA), sa taille
    put32(rsrc, 0x4C, static_cast<std::uint32_t>(vi.size()));
    rsrc += vi;
    const std::size_t rawSize = (rsrc.size() + 0x1FF) & ~static_cast<std::size_t>(0x1FF);
    rsrc.resize(rawSize, '\0');

    std::string pe(0x200, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(pe, 0x3C, 0x40);
    pe.replace(0x40, 4, std::string("PE\0\0", 4));
    put16(pe, 0x44, 0x014C);   // i386
    put16(pe, 0x46, 1);        // une section
    put16(pe, 0x54, 224);      // l'en-tete optionnel
    put16(pe, 0x56, 0x0102);
    const std::size_t opt = 0x58;
    put16(pe, opt, 0x10B);
    put32(pe, opt + 28, 0x400000);
    put32(pe, opt + 32, 0x1000);
    put32(pe, opt + 36, 0x200);
    put32(pe, opt + 56, 0x1000 + static_cast<std::uint32_t>((rawSize + 0xFFF) & ~static_cast<std::size_t>(0xFFF)));
    put32(pe, opt + 60, 0x200);
    put16(pe, opt + 68, 2);
    put32(pe, opt + 92, 16);
    if (withVersion) {
        put32(pe, opt + 96 + 16, 0x1000);
        put32(pe, opt + 96 + 20, 0x58 + static_cast<std::uint32_t>(vi.size()));
    }
    const std::size_t sec = opt + 224;
    pe.replace(sec, 8, std::string(".rsrc\0\0\0", 8));
    put32(pe, sec + 8, static_cast<std::uint32_t>(0x58 + vi.size()));
    put32(pe, sec + 12, 0x1000);
    put32(pe, sec + 16, static_cast<std::uint32_t>(rawSize));
    put32(pe, sec + 20, 0x200);
    put32(pe, sec + 36, 0x40000040u);
    return pe + rsrc + overlay;
}

// Les lignes d'une table de la sauvegarde d'un service (telles quelles), page par page.
std::vector<std::string> tableRows(const std::string& base, const lic::SigningKey& key, const std::string& table) {
    std::vector<std::string> out;
    std::string cursor;
    for (int guard = 0; guard < 1000; ++guard) {
        const Reply r = call(base, "GET", "/admin/sauvegarde?table=" + table + (cursor.empty() ? std::string() : "&depuis=" + cursor), {}, &key);
        if (r.status != 200 || !r.value.isObject()) break;
        if (const json::Value* rows = r.value.find("lignes"); rows && rows->isArray())
            for (const auto& x : rows->items()) out.push_back(json::write(x, false));
        const json::Value* next = r.value.find("suivant");
        if (!next || !next->isString() || next->asString().empty()) break;
        cursor = next->asString();
    }
    return out;
}

// Les messages d'un PC (GET /v1/messages, signe par sa cle).
std::vector<std::string> messagesOf(const std::string& base, const lic::SigningKey& pc) {
    std::vector<std::string> out;
    const Reply r = call(base, "GET", "/v1/messages?depuis=0", {}, &pc);
    if (r.status == 200 && r.value.isObject())
        if (const json::Value* m = r.value.find("messages"); m && m->isArray())
            for (const auto& x : m->items()) out.push_back(x.str("bloc"));
    return out;
}

// Ouvrir un message (le bloc signe par l'editeur, son contenu scelle pour ce PC).
bool openMessage(const std::string& block, const lic::SigningKey& editor, lic::LicenseStore& pc, adm::Message& m, adm::MessageContent& c, std::string& why) {
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(block, blocks, &why) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::Message) return false;
    if (!lic::verifyBlock(blocks[0], editor.publicKey())) {
        why = "signature";
        return false;
    }
    if (!adm::parseMessage(blocks[0].document, m, &why)) return false;
    std::string plain;
    if (!pc.deviceOpenSeal(m.sealed, plain, &why)) return false;
    return adm::parseMessageContent(plain, c, &why);
}

// Telecharger un fichier entier morceau par morceau (`query` : &jeton=... ; `adminToken` : X-XPG-Jeton).
std::string download(const std::string& base, const std::string& id, std::uint64_t size, const std::string& query, int* lastStatus = nullptr) {
    std::string out;
    const std::uint64_t chunks = (size + 1048575) / 1048576;
    for (std::uint64_t n = 0; n < chunks; ++n) {
        const Reply r = call(base, "GET", "/v1/fichiers/morceau?id=" + id + "&n=" + std::to_string(n) + query, {}, nullptr);
        if (lastStatus) *lastStatus = r.status;
        if (r.status != 200) return {};
        out += r.body;
    }
    return out;
}

const char* kPhrase = "phrase d'essai de la sauvegarde 2026 !";

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : std::string(".");
    if (!lic::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-admins-fichiers-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};

    // ======================================================================== sans reseau
    std::printf("-- la version d'un installateur (VS_VERSIONINFO), sans reseau\n");
    const fs::path inst = root / "installateurs";
    {
        std::string why;
        writeFile(inst / "a.exe", buildPe({1, 44, 1, 0}, "1.44.1", "1.44.1.0", true, randomBytes(5000, rng)));
        check(gui::Workspace::readInstallerVersion(U(inst / "a.exe"), &why) == "1.44.1", "ProductVersion << 1.44.1 >> (" + why + ")");
        writeFile(inst / "b.exe", buildPe({1, 44, 2, 0}, "", "1, 44, 2, 0", true, {}));
        check(gui::Workspace::readInstallerVersion(U(inst / "b.exe"), &why) == "1.44.2", "FileVersion << 1, 44, 2, 0 >> seule (" + why + ")");
        writeFile(inst / "c.exe", buildPe({1, 45, 0, 0}, "", "", true, {}));
        check(gui::Workspace::readInstallerVersion(U(inst / "c.exe"), &why) == "1.45.0", "les nombres seuls (VS_FIXEDFILEINFO) (" + why + ")");
        writeFile(inst / "d.exe", buildPe({1, 44, 1, 0}, "", "", false, {}));
        why.clear();
        check(gui::Workspace::readInstallerVersion(U(inst / "d.exe"), &why).empty() && has(why, "version"), "un .exe sans ressource de version : refuse (" + why + ")");
        writeFile(inst / "e.exe", "ceci n'est pas un programme");
        why.clear();
        check(gui::Workspace::readInstallerVersion(U(inst / "e.exe"), &why).empty() && has(why, "programme Windows"), "un fichier qui n'est pas un .exe : refuse (" + why + ")");
        std::string garbage = buildPe({1, 44, 1, 0}, "1.44.1", "1.44.1.0", true, {});
        garbage.resize(0x240);   // la section tronquee
        writeFile(inst / "f.exe", garbage);
        why.clear();
        check(gui::Workspace::readInstallerVersion(U(inst / "f.exe"), &why).empty() && !why.empty(), "un .exe tronque : refuse sans lire au-dela (" + why + ")");
        if (const char* real = std::getenv("XPG_ESSAI_EXE"); real && *real) {
            why.clear();
            const std::string v = gui::Workspace::readInstallerVersion(real, &why);
            std::printf("   %s : version %s %s\n", real, v.c_str(), why.c_str());
            check(!v.empty(), "un vrai installateur (XPG_ESSAI_EXE) : sa version lue (" + why + ")");
        }
        check(!gui::Workspace::describeBackup(U(inst / "e.exe")).ok, "une sauvegarde illisible : refusee");
    }

    std::printf("-- l'admin+ (une cle d'essai)\n");
    gui::Workspace plus(U(root / "plus"));
    {
        check(plus.createKey("essai fichiers", true).ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai)");
        const auto r = plus.signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json");
        check(r.ok && plus.hasRegistry(), "le registre d'exemple signe (" + r.text() + ")");
        check(!plus.backupDueToday(), "la sauvegarde n'est pas reglee : rien a faire chaque jour");
        std::string id;
        check(!plus.uploadFile(U(inst / "a.exe"), "studios", "1.44.1", "public", id).ok, "deposer sans service regle : refuse");
    }

    // ======================================================================== le service local
    const std::string editorHex = plus.signingKey()->publicKeyHex();
    xpg_test::LocalService svc({editorHex});
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    if (!svc.ready()) {
        std::printf("ECHEC : le service local : %s\n", svc.error().c_str());
        return 1;
    }
    std::printf("   service local : %s (%.1f s)\n", svc.url().c_str(), svc.startSeconds());
    const lic::SigningKey& editor = *plus.signingKey();

    {
        check(plus.serviceConfigure(svc.url()).ok, "l'adresse du service");
        check(plus.serviceCertify({}).ok, "la cle du service certifiee");
        std::string code;
        check(plus.syncKeyCreate(&code).ok, "la cle de synchro");
        auto r = plus.signRegistry(plus.registry());
        auto job = plus.takeServiceJob();
        check(r.ok && job && plus.serviceRun(*job).ok, "le registre depose");
        plus.reload();
    }

    std::printf("-- un fichier de 3,5 Mio : coupe, puis repris\n");
    const fs::path big = root / "donnees" / "gros-essai.bin";
    const std::string bigBytes = randomBytes(3 * 1048576 + 524288, rng);
    writeFile(big, bigBytes);
    std::string bigId;
    {
        int calls = 0;
        std::uint64_t lastDone = 0;
        const auto stopAfterFirst = [&](std::uint64_t done, std::uint64_t total) {
            ++calls;
            lastDone = done;
            return total == bigBytes.size() && done < 1048576;   // arrete des que le premier morceau est la
        };
        std::string id;
        auto r = plus.uploadFile(U(big), "autre", "", "jeton", id, stopAfterFirst);
        check(!r.ok && has(r.message, "arr\xC3\xAAt\xC3\xA9") && lastDone == 1048576, "l'envoi arrete apres le premier morceau (" + r.text() + ")");
        const auto sent = plus.sentFiles();
        check(sent.size() == 1 && !sent[0].complete && sent[0].manifest.size == bigBytes.size() && sent[0].manifest.chunks() == 4, "le manifeste garde pour reprendre");
        int progressCalls = 0;
        std::uint64_t first = ~0ull;
        r = plus.uploadFile(U(big), "autre", "", "jeton", bigId, [&](std::uint64_t done, std::uint64_t) {
            if (first == ~0ull) first = done;
            ++progressCalls;
            return true;
        });
        check(r.ok && bigId == sent[0].manifest.id && first == 1048576 && has(r.text(), "repris") && has(r.text(), "1 morceau \xC3\xA9tait d\xC3\xA9j\xC3\xA0 l\xC3\xA0, 3 envoy\xC3\xA9s"),
              "repris : le meme manifeste, seuls les 3 morceaux qui manquent (" + r.text() + ")");
        check(plus.sentFiles().size() == 1 && plus.sentFiles()[0].complete, "note complet");
        const Reply m = call(svc.url(), "GET", "/v1/fichiers/manifeste?id=" + bigId, {}, nullptr);
        std::vector<lic::SignedBlock> blocks;
        adm::FileManifest fm;
        check(m.status == 200 && lic::readBlocks(m.body, blocks, nullptr) && blocks.size() == 1 && lic::verifyBlock(blocks[0], editor.publicKey()) &&
                  adm::parseFileManifest(blocks[0].document, fm, nullptr) && fm.sha256 == lic::crypto::sha256Hex(bigBytes) && fm.access == "jeton",
              "le manifeste public : signe par l'editeur, le SHA-256 du fichier");
        int st = 0;
        check(download(svc.url(), bigId, bigBytes.size(), "", &st).empty() && st == 403, "sans lien : refuse (acces << jeton >>)");
        std::string tok;
        r = plus.fileToken(bigId, 3600, true, tok);
        check(r.ok && tok.size() == 32 && has(r.output, "/v1/fichiers/lien?jeton="), "un lien d'une fois (" + r.text() + ")");
        check(download(svc.url(), bigId, bigBytes.size(), "&jeton=" + tok) == bigBytes, "le fichier entier par son lien, identique");
        check(download(svc.url(), bigId, bigBytes.size(), "&jeton=" + tok, &st).empty() && st == 403, "le lien d'une fois, une seconde fois : refuse");
        std::string tok2;
        check(plus.fileToken(bigId, 999999, false, tok2).ok, "un lien de 48 h (borne)");
        const Reply link = call(svc.url(), "GET", "/v1/fichiers/lien?jeton=" + tok2, {}, nullptr);
        check(link.status == 200 && link.body == bigBytes, "GET /v1/fichiers/lien : le fichier entier");
        check(plus.fileLink(tok2) == svc.url() + "/v1/fichiers/lien?jeton=" + tok2, "l'adresse du lien");
        std::string again;
        r = plus.uploadFile(U(big), "autre", "", "jeton", again);
        check(r.ok && again == bigId && has(r.text(), "D\xC3\xA9j\xC3\xA0 entier"), "le meme fichier de nouveau : deja entier, rien ne repart");
        writeFile(root / "donnees" / "mauvais..nom.bin", "x");
        check(!plus.uploadFile(U(root / "donnees" / "mauvais..nom.bin"), "autre", "", "jeton", again).ok, "un nom de fichier refuse par le service : refuse ici");
    }

    std::printf("-- un message a un PC connu\n");
    const auto pcOptions = [&](const fs::path& folder, const std::string& machine) {
        lic::StoreOptions o;
        o.folder = U(folder);
        o.compiledKeys = false;
        lic::TrustedKey t;
        t.id = editor.keyId();
        t.name = editor.name();
        t.key = editor.publicKey();
        t.test = editor.test();
        o.trustedKeys = {t};
        o.machineId = machine;
        return o;
    };
    fs::create_directories(root / "pc-a", ec);
    fs::create_directories(root / "pc-b", ec);
    lic::LicenseStore pcA(pcOptions(root / "pc-a", "machine-essai-a")), pcB(pcOptions(root / "pc-b", "machine-essai-b"));
    std::string why;
    check(pcA.writeActivationRequest(U(root / "pc-a.xpgreq"), "Poste essai A", "", "IHM Studio 1.44.0 (essai)", &why) &&
              pcB.writeActivationRequest(U(root / "pc-b.xpgreq"), "Poste essai B", "", "API Studio 1.43.0 (essai)", &why),
          "deux PC d'essai font une demande (" + why + ")");
    auto keyA = lic::SigningKey::load(U(root / "pc-a" / lic::kDeviceKeyFile), lic::deviceKeySpec(), &why);
    auto keyB = lic::SigningKey::load(U(root / "pc-b" / lic::kDeviceKeyFile), lic::deviceKeySpec(), &why);
    check(keyA && keyB, "les cles des deux PC (" + why + ")");
    if (!keyA || !keyB) return finish();
    const std::string idA = sv::deviceId(keyA->publicKey()), idB = sv::deviceId(keyB->publicKey());
    {
        check(plus.recordRequests({U(root / "pc-a.xpgreq"), U(root / "pc-b.xpgreq")}).ok, "les demandes notees");
        plus.reload();
        const auto pcs = plus.pcsForMessages();
        const auto find = [&](const std::string& id) -> const gui::MessagePc* {
            for (const auto& p : pcs)
                if (p.deviceId == id) return &p;
            return nullptr;
        };
        check(pcs.size() == 2 && find(idA) && find(idA)->name == "Poste essai A" && find(idA)->studioVersion == "1.44.0" && find(idB) &&
                  find(idB)->studioVersion == "1.43.0" && find(idA)->deviceKey == keyA->publicKeyHex(),
              "les PC a qui ecrire : leur nom, leur Studio, leur cle");
        writeFile(root / "donnees" / "notice-essai.txt", "Une notice d'essai.\n");
        adm::MessageFile f;
        auto r = plus.attachFile(U(root / "donnees" / "notice-essai.txt"), f);
        check(r.ok && f.token.size() == 32 && f.size == 20, "un fichier a joindre (" + r.text() + ")");
        int number = 0;
        check(!plus.sendMessageToPc("0123456789abcdef0123456789abcdef", "Essai", "x", {}, "", &number).ok, "un PC inconnu : refuse");
        check(!plus.sendMessageToPc(idA, "", "x", {}, "").ok, "sans titre : refuse");
        r = plus.sendMessageToPc(idA, "Message d'essai", "Bonjour, ceci est un essai.", {f}, "", &number);
        check(r.ok && number > 0, "le message depose (" + r.text() + ")");
        const auto forA = messagesOf(svc.url(), *keyA);
        const auto forB = messagesOf(svc.url(), *keyB);
        adm::Message m;
        adm::MessageContent c;
        check(forA.size() == 1 && openMessage(forA[0], editor, pcA, m, c, why) && m.device == idA && m.title == "Message d'essai" &&
                  c.text == "Bonjour, ceci est un essai." && c.files.size() == 1 && c.files[0].name == "notice-essai.txt" && c.action.empty(),
              "le PC A le recoit et l'ouvre (" + why + ")");
        if (c.files.size() == 1)
            check(download(svc.url(), c.files[0].id, c.files[0].size, "&jeton=" + c.files[0].token) == "Une notice d'essai.\n", "son fichier joint, par le lien du message");
        adm::Message m2;
        adm::MessageContent c2;
        std::string w2;
        check(forB.empty() && (forA.empty() || !openMessage(forA[0], editor, pcB, m2, c2, w2)), "le PC B ne le recoit pas, et ne pourrait pas l'ouvrir");
    }

    std::printf("-- une version publiee\n");
    const fs::path studiosExe = inst / "API-IHM-Studio-Setup-1.44.1.exe", adminExe = inst / "XPG-Admin-Setup-1.44.1.exe";   // 1.49.0 : l'Admin des admins (XPG Admin)
    writeFile(studiosExe, buildPe({1, 44, 1, 0}, "1.44.1", "1.44.1.0", true, randomBytes(1048576 + 300000, rng)));
    writeFile(adminExe, buildPe({1, 44, 1, 0}, "1.44.1", "1.44.1.0", true, randomBytes(120000, rng)));
    const std::string adminBytes = readFile(adminExe);
    {
        writeFile(inst / "studios-1.44.2.exe", buildPe({1, 44, 2, 0}, "1.44.2", "1.44.2.0", true, {}));
        auto r = plus.publishVersion(U(inst / "studios-1.44.2.exe"), U(adminExe), {}, false);
        check(!r.ok && has(r.message, "m\xC3\xAAme version"), "deux installateurs de versions differentes : refuse (" + r.message + ")");
        int calls = 0;
        std::uint64_t last = 0, total = 0;
        r = plus.publishVersion(U(studiosExe), U(adminExe), {"Exports PDF : plus rapides (essai).", "", "Corrige : un essai."}, false,
                                [&](std::uint64_t done, std::uint64_t t) {
                                    ++calls;
                                    last = done;
                                    total = t;
                                    return true;
                                });
        check(r.ok && last == total && total == fs::file_size(studiosExe) + fs::file_size(adminExe), "la version 1.44.1 publiee, l'avancement des deux (" + r.text() + ")");
        const Reply v = call(svc.url(), "GET", "/v1/version", {}, nullptr);
        std::vector<lic::SignedBlock> blocks;
        adm::VersionManifest vm;
        check(v.status == 200 && lic::readBlocks(v.body, blocks, nullptr) && blocks.size() == 1 && lic::verifyBlock(blocks[0], editor.publicKey()) &&
                  adm::parseVersion(blocks[0].document, vm, nullptr) && vm.version == "1.44.1" && vm.notes.size() == 2 && vm.files.size() == 2 &&
                  vm.file("studios") && vm.file("studios")->audience == "tous" && vm.file("admin") && vm.file("admin")->audience == "admins" &&
                  vm.file("admin")->sha256 == lic::crypto::sha256Hex(adminBytes),
              "GET /v1/version : signee par l'editeur, les deux fichiers, leur public, les notes");
        const Reply etat = call(svc.url(), "GET", "/v1/etat", {}, nullptr);
        check(etat.value.str("version_publiee") == "1.44.1", "GET /v1/etat : version_publiee");
        gui::VersionsView view;
        r = plus.versions(view);
        check(r.ok && view.latest() == "1.44.1" && view.versions.size() == 1 && view.versions[0].files.size() == 2 && view.versions[0].files[0].present &&
                  view.versions[0].files[1].present && !view.versions[0].files[0].localPath.empty() && view.versions[0].notes.size() == 2,
              "les versions publiees (" + r.text() + ")");
        bool sawPlus = false;
        for (const auto& a : view.admins) sawPlus = sawPlus || (a.name == "admin+" && a.software == "XPG-License-Admin");
        check(sawPlus && view.total >= 1, "qui est a jour : l'Admin de l'admin+ vu par le service");
        r = plus.publishVersion(U(studiosExe), U(adminExe), {}, false);
        check(!r.ok && has(r.message, "pas plus r\xC3\xA9" "cente"), "la meme version de nouveau : refusee (" + r.message + ")");
        writeFile(inst / "s-1.44.0.exe", buildPe({1, 44, 0, 0}, "1.44.0", "1.44.0.0", true, {}));
        writeFile(inst / "a-1.44.0.exe", buildPe({1, 44, 0, 0}, "1.44.0", "1.44.0.0", true, {}));
        r = plus.publishVersion(U(inst / "s-1.44.0.exe"), U(inst / "a-1.44.0.exe"), {}, false);
        check(!r.ok && has(r.message, "1.44.0") && has(r.message, "pas plus r\xC3\xA9" "cente"), "une version plus ancienne : refusee avant tout envoi");
        check(plus.suggestedNotes("1.44.1").empty(), "pas de notes proposees (l'Admin n'embarque pas celles des Studios)");
    }

    std::printf("-- l'installateur de l'Admin : dans les notifications d'un PC, et par un lien\n");
    {
        auto r = plus.sendAdminInstaller(idB, "L\xC3\xA9" "a Essai", "");
        check(r.ok && has(r.text(), "1.44.1"), "envoye au PC B (celui de la version publiee) (" + r.text() + ")");
        const auto forB = messagesOf(svc.url(), *keyB);
        adm::Message m;
        adm::MessageContent c;
        check(forB.size() == 1 && openMessage(forB[0], editor, pcB, m, c, why) && c.action == "admin" && c.files.size() == 1 && c.files[0].version == "1.44.1" &&
                  has(c.text, "L\xC3\xA9" "a Essai"),
              "le PC B l'ouvre : l'action << admin >>, l'installateur 1.44.1 (" + why + ")");
        if (c.files.size() == 1) {
            int st = 0;
            check(download(svc.url(), c.files[0].id, c.files[0].size, "", &st).empty() && st == 403, "l'installateur de l'Admin sans lien : reserve aux admins");
            check(download(svc.url(), c.files[0].id, c.files[0].size, "&jeton=" + c.files[0].token) == adminBytes, "par le lien du message : le fichier, identique");
        }
        std::string url;
        r = plus.adminInstallerLink("", url);
        check(r.ok && has(url, svc.url() + "/v1/fichiers/lien?jeton="), "un lien d'une fois (" + r.text() + ")");
        const std::string path = url.substr(svc.url().size());
        const Reply first = call(svc.url(), "GET", path, {}, nullptr);
        const Reply second = call(svc.url(), "GET", path, {}, nullptr);
        check(first.status == 200 && first.body == adminBytes && second.status == 403, "le lien : le fichier une fois, puis refuse");
    }

    std::printf("-- de quoi sauvegarder : une licence en ligne, un admin, le journal\n");
    std::string onlineCode;
    gui::Workspace lea(U(root / "lea"));
    {
        gui::LicenceDraft d;
        d.online = true;
        d.licenseId = "XPG-ESSAI-SAUVE-1";
        d.customer = "Client d'essai sauvegarde";
        d.edition = "PERSONAL";
        d.maxDevices = 2;
        d.durationDays = 90;
        gui::ServiceJob job;
        auto r = plus.prepareOnlineLicence(d, {}, job, onlineCode);
        check(r.ok && !onlineCode.empty(), "une licence en ligne preparee (" + r.text() + ")");
        r = plus.serviceRunOnline(job, onlineCode);
        check(r.ok, "deposee, son code garde sur ce poste (" + r.text() + ")");
        check(lea.serviceConfigure(svc.url()).ok, "le second poste : l'adresse du service");
        std::string code;   // 1.51.0 : l'invitation, le code, la signature d'office (plus de demande d'admin)
        check(plus.inviteAdmin("L\xC3\xA9" "a Essai", "lea@exemple.invalid", "Tous les droits", code).ok, "Lea invitee");
        r = lea.joinWithInvitation(code);
        lea.reload();
        check(r.ok && lea.adminKey().present, "son code : inscrite (" + r.text() + ")");
        r = plus.admitInvitedAdmins();
        check(r.ok, "signee d'office (" + r.text() + ")");
        plus.reload();
        check(plus.fetchJournal().ok && !plus.journalLines().rows.empty(), "le journal releve");
        const auto sync = plus.archiveSync(false, false);
        check(sync.ok, "l'archive synchronisee (" + sync.why + ")");
    }

    std::printf("-- les adresses de secours\n");
    {
        const auto before = plus.registry().number;
        check(!plus.setFallbackServices({"ftp://secours.exemple.dev"}).ok, "une adresse qui n'est pas https : refusee");
        check(!plus.setFallbackServices({"https://a.exemple.dev", "https://b.exemple.dev", "https://c.exemple.dev", "https://d.exemple.dev"}).ok, "4 adresses : refuse");
        check(!plus.setFallbackServices({svc.url()}).ok, "l'adresse principale : refusee");
        check(!plus.setFallbackServices({"https://a.exemple.dev", "https://a.exemple.dev/"}).ok, "deux fois la meme : refuse");
        auto r = plus.setFallbackServices({"https://secours-1.exemple.dev", "https://secours-2.exemple.dev:8443"});
        check(r.ok && plus.registry().number == before + 1 && plus.fallbackServices().size() == 2 && plus.fallbackServices()[0] == "https://secours-1.exemple.dev",
              "deux adresses : un registre n\xC2\xB0 + 1 (" + r.text() + ")");
        auto job = plus.takeServiceJob();
        check(job && plus.serviceRun(*job).ok, "le registre depose");
        const Reply reg = call(svc.url(), "GET", "/v1/registre", {}, nullptr);
        check(reg.status == 200 && has(reg.body, "secours-2.exemple.dev:8443"), "le service a le registre avec les adresses");
        check(plus.setFallbackServices(plus.fallbackServices()).ok && plus.registry().number == before + 1, "les memes : rien ne change");
    }

    std::printf("-- la sauvegarde\n");
    const fs::path backups = root / "sauvegardes";
    la::KeyBackupOptions weak;   // les essais : Argon2id au plus bas permis (2 passes, 64 Mio)
    weak.opsLimit = 2;
    weak.memLimit = 64u * 1024u * 1024u;
    std::string backupPath;
    std::string syncCode;
    {
        check(!plus.configureBackup(U(backups), "court", weak).ok, "une phrase trop courte : refusee");
        auto r = plus.configureBackup(U(backups), kPhrase, weak);
        check(r.ok, "reglee : le dossier, la phrase (" + r.text() + ")");
        check(!has(readFile(root / "plus" / "sauvegarde.json"), kPhrase) && !has(readFile(root / "plus" / "sauvegarde.cle"), kPhrase), "la phrase n'est ecrite nulle part");
        check(plus.backupSettings().configured && plus.backupDueToday(), "la sauvegarde du jour est a faire");
        for (int i = 0; i < 31; ++i) {   // des sauvegardes plus anciennes (les 30 dernieres restent)
            const fs::path old = backups / ("sauvegarde-licences-2026-01-" + std::string(i < 9 ? "0" : "") + std::to_string(i + 1) + "-0300.xpgsauve");
            writeFile(old, "ancienne");
            fs::last_write_time(old, fs::file_time_type::clock::now() - std::chrono::hours(24 * (40 - i)), ec);
        }
        (void)plus.syncRecoveryCode(&why);
        syncCode = plus.syncRecoveryCode(&why);
        r = plus.backupNow(&backupPath);
        check(r.ok && fs::exists(backupPath) && has(U(fs::path(backupPath).filename()), "sauvegarde-licences-") && fs::path(backupPath).extension() == ".xpgsauve",
              "sauvegarde faite (" + r.text() + ")");
        const std::string text = readFile(backupPath);
        check(text.rfind("XPG-SAUVEGARDE 1\ncle=" + editor.keyId() + "\ncreee=", 0) == 0 && has(text, " service=" + svc.url() + "\nargon2id ops=2 mem=67108864 sel="),
              "l'en-tete : la cle, l'heure, le service, Argon2id");
        check(!has(text, onlineCode) && (syncCode.empty() || !has(text, syncCode)) && !has(text, "Client d'essai sauvegarde") && !has(text, kPhrase),
              "rien en clair : ni code, ni cle de synchro, ni client, ni phrase");
        std::size_t kept = 0;
        for (const auto& e : fs::directory_iterator(backups, ec))
            if (e.path().extension() == ".xpgsauve") ++kept;
        check(kept == 30 && !fs::exists(backups / "sauvegarde-licences-2026-01-01-0300.xpgsauve") && !fs::exists(backups / "sauvegarde-licences-2026-01-02-0300.xpgsauve"),
              "les 30 dernieres gardees (" + std::to_string(kept) + ")");
        plus.reload();
        check(!plus.backupDueToday() && plus.backupSettings().lastPath == backupPath, "faite aujourd'hui : plus a faire");
        gui::BackupInfo info;
        r = gui::Workspace::describeBackup(backupPath, &info);
        check(r.ok && info.keyId == editor.keyId() && info.serviceUrl == svc.url() && info.created > 0, "decrite sans la phrase (" + r.text() + ")");
        gui::Workspace wrong(U(root / "mauvaise-phrase"));
        r = wrong.restoreBackup(backupPath, "une autre phrase de passe 2026", {}, nullptr);
        check(!r.ok && has(r.message, "phrase") && !fs::exists(root / "mauvaise-phrase" / la::kPublisherKeyFile), "une autre phrase : refusee, rien n'est ecrit");
        std::string tampered = text;
        tampered[tampered.find("creee=") + 6] ^= 1;
        writeFile(root / "modifiee.xpgsauve", tampered);
        r = wrong.restoreBackup(U(root / "modifiee.xpgsauve"), kPhrase, {}, nullptr);
        check(!r.ok, "l'en-tete modifie : refusee (les lignes sont liees au chiffre)");
    }

    std::printf("-- restaurer dans un poste neuf et un SECOND service neuf\n");
    xpg_test::LocalService svc2({editorHex});
    if (!svc2.ready()) {
        std::printf("ECHEC : le second service local : %s\n", svc2.error().c_str());
        return finish();
    }
    std::printf("   second service local : %s\n", svc2.url().c_str());
    gui::Workspace restored(U(root / "restaure"));
    {
        gui::RestoreOptions o;
        o.restoreService = true;
        o.serviceUrl = svc2.url();
        gui::RestoreReport rep;
        auto r = restored.restoreBackup(backupPath, kPhrase, o, &rep);
        check(r.ok && rep.serviceRestored && rep.editorKey && rep.syncKey && rep.codes == 1 && rep.files > 5 && !rep.serviceKey.empty() && rep.filesToResend >= 2,
              "restauree : le dossier, la cle, la cle de synchro, le code, le service (" + r.text() + ")");
        restored.reload();
        check(restored.role() == gui::Role::Plus && restored.signingKey() && restored.signingKey()->publicKeyHex() == editorHex, "le poste neuf est l'admin+ (la meme cle)");
        check(restored.licences().size() == plus.licences().size() && restored.licence("XPG-ESSAI-SAUVE-1") && restored.adminList() && plus.adminList() &&
                  restored.adminList()->number == plus.adminList()->number,
              "les memes licences et la meme liste des admins sur le poste");
        check(restored.service().config.url == svc2.url() && restored.service().syncKey && restored.service().syncKeyId == plus.service().syncKeyId, "le service : la nouvelle adresse ; la meme cle de synchro");
        std::string code;
        check(restored.revealCode("XPG-ESSAI-SAUVE-1", code).ok && code == onlineCode, "le code d'activation garde de nouveau (protege pour ce compte)");
        check(!fs::exists(root / "restaure" / "jeton-admin.xpgjet") && fs::exists(root / "restaure" / "sauvegarde.cle") && restored.backupSettings().configured,
              "pas de jeton d'admin recopie ; la sauvegarde de chaque jour reglee sur ce poste");
        // les deux services, exportes et compares
        // (le nombre de lignes attendu au moins : une table vide des deux cotes ne prouverait rien)
        const std::map<std::string, std::size_t> atLeast = {{"licences", 1}, {"activations", 0}, {"admins", 1},       {"trousseau", 1}, {"postes", 0},
                                                            {"archive", 1},  {"messages", 2},    {"versions", 1},     {"demandes", 0},  {"fichiers_jetons", 3}};
        for (const auto& [t, n] : atLeast) {
            const auto a = tableRows(svc.url(), editor, t), b = tableRows(svc2.url(), editor, t);
            check(a == b && a.size() >= n, "la table " + t + " : la meme (" + std::to_string(a.size()) + " / " + std::to_string(b.size()) + " lignes)");
        }
        {   // les fichiers : les memes manifestes (sans leurs octets : incomplets ici)
            const auto a = tableRows(svc.url(), editor, "fichiers"), b = tableRows(svc2.url(), editor, "fichiers");
            const auto manifests = [](const std::vector<std::string>& rows) {
                std::vector<std::string> out;
                for (const auto& row : rows) {
                    json::Value v;
                    if (json::parse(row, v, nullptr)) out.push_back(v.str("id") + v.str("manifeste"));
                }
                return out;
            };
            check(a.size() >= 4 && manifests(a) == manifests(b), "la table fichiers : les memes manifestes (" + std::to_string(a.size()) + ")");
        }
        const auto ja = tableRows(svc.url(), editor, "journal"), jb = tableRows(svc2.url(), editor, "journal");
        bool sameJournal = !jb.empty() && jb.size() <= ja.size() + 1;
        for (std::size_t i = 0; sameJournal && i + 1 < jb.size(); ++i) sameJournal = ja[i] == jb[i];
        check(sameJournal && jb.size() >= 10 && has(jb.back(), "restauration"), "le journal : les memes lignes, puis << restauration >> (" + std::to_string(ja.size()) + " / " + std::to_string(jb.size()) + ")");
        const Reply k1 = call(svc.url(), "GET", "/admin/cle-service", {}, &editor), k2 = call(svc2.url(), "GET", "/admin/cle-service", {}, &editor);
        check(k1.value.str("cle") == k2.value.str("cle") && k2.value.str("cle") == rep.serviceKey, "la meme cle de service");
        on::ServiceState st;
        r = restored.serviceVerify(&st);
        check(r.ok && st.certOk && st.certUrl == svc2.url(), "le certificat signe de nouveau pour la nouvelle adresse (" + r.text() + ")");
        check(call(svc2.url(), "GET", "/v1/version", {}, nullptr).body == call(svc.url(), "GET", "/v1/version", {}, nullptr).body, "la meme version publiee");
        // la cle remise signe, le second service l'accepte
        r = restored.signRegistry(restored.registry());
        auto job = restored.takeServiceJob();
        check(r.ok && job && restored.serviceRun(*job).ok, "un registre signe par la cle remise : le second service l'accepte");
        const Reply reg = call(svc2.url(), "GET", "/v1/registre", {}, nullptr);
        check(reg.status == 200 && has(reg.body, "secours-1.exemple.dev"), "le second service a ce registre (et les adresses de secours)");
        // les fichiers a renvoyer
        std::vector<gui::FileToResend> files;
        r = restored.filesToResend(files);
        check(r.ok && files.size() == 2 && !files[0].localPath.empty() && !files[1].localPath.empty(), "les deux installateurs a renvoyer, connus sur ce poste (" + r.text() + ")");
        for (const auto& f : files) {
            r = restored.resendFile(f.file.id, f.localPath);
            check(r.ok, "renvoye : " + f.file.name + " (" + r.text() + ")");
        }
        gui::VersionsView view;
        check(restored.versions(view).ok && view.versions.size() == 1 && view.versions[0].files[0].present && view.versions[0].files[1].present,
              "le second service a de nouveau les deux installateurs");
        check(!restored.resendFile(files.empty() ? std::string(32, '0') : files[0].file.id, U(big)).ok, "un autre fichier a sa place : refuse");
        // le second service n'est plus vide : refuse, puis << Remplacer le service >>
        rep = gui::RestoreReport{};
        r = restored.restoreBackup(backupPath, kPhrase, o, &rep);
        check(!r.ok && rep.serviceNotEmpty && has(r.message, "pas vide"), "restaurer dans un service qui n'est pas vide : refuse (" + r.message + ")");
        o.replaceService = true;
        r = restored.restoreBackup(backupPath, kPhrase, o, &rep);
        check(r.ok && rep.serviceRestored, "avec << Remplacer le service >> : fait (" + r.message + ")");
        check(tableRows(svc.url(), editor, "licences") == tableRows(svc2.url(), editor, "licences"), "les memes licences de nouveau");
        // seulement le service : le dossier de ce poste (plus recent que la sauvegarde) ne recule pas
        const fs::path acts = root / "restaure" / "activations.jsonl";
        const std::string before = readFile(acts) + "{\"kind\":\"essai\"}\n";
        writeFile(acts, before);
        o.restoreFolder = false;
        r = restored.restoreBackup(backupPath, kPhrase, o, &rep);
        check(r.ok && rep.serviceRestored && rep.files == 0 && readFile(acts) == before, "seulement le service : fait, le dossier du poste reste tel quel (" + r.message + ")");
    }

    std::printf("-- une autre cle de l'editeur deja la\n");
    {
        gui::Workspace other(U(root / "autre-cle"));
        check(other.createKey("autre cle d'essai", true).ok, "une autre cle d'essai");
        const std::string otherHex = other.signingKey()->publicKeyHex();
        gui::RestoreOptions serviceOnly;
        serviceOnly.restoreFolder = false;
        serviceOnly.restoreService = true;
        serviceOnly.serviceUrl = svc2.url();
        auto r = other.restoreBackup(backupPath, kPhrase, serviceOnly, nullptr);
        check(!r.ok && has(r.message, "pas la cl\xC3\xA9"), "seulement le service depuis un poste qui n'a pas cette cle : refuse (" + r.message + ")");
        r = other.restoreBackup(backupPath, kPhrase, {}, nullptr);
        check(!r.ok && has(r.message, "AUTRE cl\xC3\xA9") && !fs::exists(root / "autre-cle" / "admins.xpgadm"), "refuse, rien n'est ecrit (" + r.message + ")");
        gui::RestoreOptions o;
        o.replaceKey = true;
        r = other.restoreBackup(backupPath, kPhrase, o, nullptr);
        other.reload();
        bool setAside = false;
        for (const auto& e : fs::directory_iterator(root / "autre-cle", ec))
            if (has(U(e.path().filename()), std::string(la::kPublisherKeyFile) + ".remplacee-")) setAside = true;
        check(r.ok && other.signingKey() && other.signingKey()->publicKeyHex() == editorHex && setAside && otherHex != editorHex,
              "<< Remplacer la cle >> : la cle de la sauvegarde, l'autre mise de cote (" + r.text() + ")");
        check(other.journalWaiting() >= 1 && has(readFile(root / "autre-cle" / "journal-a-envoyer.jsonl"), "restauration"),
              "le poste restaure sans le service : une ligne << restauration >> au journal a envoyer");
    }
    return finish();
}
