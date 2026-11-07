// =============================================================================
//  tests/licensing_test.cpp - 1.30.0 (prepare en 1.29.0) : les licences hors ligne
// -----------------------------------------------------------------------------
//  Tout se passe dans un dossier temporaire, avec une cle d'editeur d'ESSAI creee
//  pour l'occasion (jamais la cle compilee : PublisherKey.hpp) et des PC simules
//  (un dossier de licences chacun, chacun sa paire de cles). L'heure est donnee
//  par l'essai (StoreOptions::clock) : on avance, on recule.
//    - les formats : dates, enveloppe signee (tronquee, genre inconnu, CRLF) ;
//    - la cle de l'editeur (essai, marquee partout), PublisherKey.hpp genere ;
//    - le registre (tests/fixtures/licences/registre-exemple.json : les editions
//      et fonctionnalites de la maquette), la licence, les revocations, signes
//      puis verifies ;
//    - chaque refus : signature fausse, cle inconnue, autre PC, expiree, en grace,
//      au-dela de la grace, revoquee, registre plus ancien, liste plus ancienne,
//      horloge reculee, fichier tronque, champ en double, champ inconnu, edition
//      inconnue, licence flottante, service en ligne, emission plus ancienne,
//      demande modifiee, trop de PC ;
//    - les fonctionnalites par edition (la matrice de la maquette), FREE sans
//      licence, les limites, l'API (isFeatureEnabled, getEdition...) ;
//    - l'outil Admin pilote en sous-processus de bout en bout (cle, registre,
//      demande -> licence -> import -> verification, revocation) ;
//    - le controle du paquet (outils/mingw/package_mingw.py --controler) : un
//      dossier de livraison avec un fichier Admin -> la construction s'arrete.
//
//    - 1.30.0 : les deux fichiers de cles (PublisherKey.hpp : les vraies, vide
//      aujourd'hui ; TestPublisherKey.hpp : la cle d'essai, seulement avec
//      XPG_LICENCES_CLE_ESSAI), exporter-cle (vide, plusieurs cles, --garder) ;
//      un programme sans cle (NotConfigured : tout ouvert, import refuse, demande
//      ecrite) ; l'ecran (registre, PC lies, historique borne, dossier non
//      inscriptible) ; la marque d'essai absente de la bibliotheque sans l'option.
//
//  licensing_test <XPG-License-Admin> <racine du depot> [<bibliotheque xpg_licensing>]   (XPG_VERBOSE=1 : chaque controle)
//  Aucune donnee client : un << Client d'essai >> et des PC nommes << Poste essai >>.
// =============================================================================
#include "../src/core/Process.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Licensing.hpp"
#include "../src/licensing/PublisherKey.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace lic = licensing;
namespace adm = license_admin;
using lic::LicenseStatus;
using lic::Seconds;

namespace {

int g_checks = 0, g_failures = 0, g_skipped = 0;
bool g_verbose = false;
Seconds g_now = 0;

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

bool isAscii(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

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

Seconds T(const char* iso) {
    Seconds t = 0;
    if (!lic::parseIsoTime(iso, t)) std::printf("  (date d'essai illisible : %s)\n", iso);
    return t;
}

// ---- le monde de l'essai ----
fs::path g_root;                 // le dossier temporaire
std::string g_repo;              // la racine du depot
std::unique_ptr<lic::SigningKey> g_key;   // la cle d'editeur d'ESSAI
std::string g_registry;          // le registre signe (n. 318)

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

lic::StoreOptions optionsFor(const fs::path& folder) {
    lic::StoreOptions o;
    o.folder = U(folder);
    o.trustedKeys = {trustedOf(*g_key)};
    o.clock = [] { return g_now; };
    return o;
}

// Une demande d'activation de ce PC.
std::string requestOf(const fs::path& pcFolder, const std::string& name, const std::string& licenseId = {}) {
    lic::LicenseStore store(optionsFor(pcFolder));
    const fs::path out = pcFolder.parent_path() / (pcFolder.filename().string() + ".xpgreq");
    std::string why;
    if (!store.writeActivationRequest(U(out), name, licenseId, "IHM Studio (essai)", &why)) std::printf("  (demande : %s)\n", why.c_str());
    return readFile(out);
}

// Signer une licence (l'Admin, dans ce processus).
std::string signLicense(const std::string& params, const std::vector<std::string>& requests, std::string* why = nullptr,
                        const std::string& existing = {}, const std::string& revocations = {}) {
    adm::LicenseInputs in;
    in.params = params;
    in.requests = requests;
    in.registry = g_registry;
    in.existing = existing;
    in.revocations = revocations;
    std::string out, w, summary;
    if (!adm::signLicense(*g_key, in, g_now, out, &w, &summary)) {
        if (why) *why = w;
        else std::printf("  (signature refusee : %s)\n", w.c_str());
        return {};
    }
    return out;
}

std::string params(const std::string& edition, const std::string& extra = {}, const std::string& id = "XPG-ESSAI-0001") {
    return "{\"license_id\": \"" + id + "\", \"customer\": \"Client d'essai\", \"edition\": \"" + edition + "\"" + (extra.empty() ? "" : ", " + extra) + "}";
}

// Signer un document fabrique a la main (pour les refus que l'Admin n'ecrirait jamais).
std::string signRaw(lic::DocKind kind, const std::string& json, const lic::SigningKey& key) {
    return lic::writeBlock(lic::signBlock(kind, json, key.publicKey(), [&](std::string_view m) { return key.sign(m); }));
}

std::string revocations(int number, const std::string& entries) {
    std::string out, why;
    if (!adm::signRevocations(*g_key, "{\"number\": " + std::to_string(number) + ", \"revoked\": [" + entries + "]}", g_now, out, &why))
        std::printf("  (revocations refusees : %s)\n", why.c_str());
    return out;
}

// Le chemin d'un programme du PATH (le sous-processus n'a pas d'environnement : un chemin complet).
std::string findInPath(const std::string& name) {
    const char* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    const char sep = ';';
    const std::vector<std::string> exts = {".exe", ""};
#else
    const char sep = ':';
    const std::vector<std::string> exts = {""};
#endif
    std::string all = path;
    std::size_t pos = 0;
    while (pos <= all.size()) {
        std::size_t end = all.find(sep, pos);
        if (end == std::string::npos) end = all.size();
        const std::string dir = all.substr(pos, end - pos);
        pos = end + 1;
        if (dir.empty()) continue;
        for (const auto& e : exts) {
            std::error_code ec;
            const fs::path p = fs::path(dir) / (name + e);
            if (fs::is_regular_file(p, ec)) return U(p);
        }
    }
    return {};
}

core::process::Result runProgram(const std::string& program, const std::vector<std::string>& args, double seconds = 60) {
    core::process::Request r;
    r.program = program;
    r.args = args;
    r.limits.seconds = seconds;
    r.limits.memoryBytes = 1024u * 1024u * 1024u;
    return core::process::run(r);
}

// ================================================================== les formats ==
void testFormats() {
    std::printf("-- les formats\n");
    Seconds t = 0;
    check(lic::parseIsoTime("2026-11-06T10:12:44Z", t) && lic::isoTime(t) == "2026-11-06T10:12:44Z", "date ISO : aller et retour");
    check(lic::parseIsoTime("2028-02-29T00:00:00Z", t) && lic::isoDate(t) == "2028-02-29", "date ISO : 29 fevrier bissextile");
    check(!lic::parseIsoTime("2027-02-29T00:00:00Z", t), "date ISO : 29 fevrier non bissextile refuse");
    check(!lic::parseIsoTime("2026-11-06T10:12:44+01:00", t) && !lic::parseIsoTime("2026-11-06", t) && !lic::parseIsoTime("2026-13-01T00:00:00Z", t),
          "date ISO : decalage, date seule, mois 13 refuses");
    check(lic::isoTime(0) == "1970-01-01T00:00:00Z", "date ISO : l'origine");

    // L'enveloppe.
    lic::SignedBlock b = lic::signBlock(lic::DocKind::Revocations, "{\"x\": 1}\n", g_key->publicKey(), [](std::string_view m) { return g_key->sign(m); });
    const std::string text = lic::writeBlock(b);
    std::vector<lic::SignedBlock> blocks;
    std::string why;
    check(lic::readBlocks(text, blocks, &why) && blocks.size() == 1 && blocks[0].document == b.document && lic::verifyBlock(blocks[0], g_key->publicKey()),
          "enveloppe : relue, signature bonne");
    check(lic::readBlocks(text + "\n" + text, blocks, &why) && blocks.size() == 2, "enveloppe : deux blocs bout a bout");
    bool allTruncRefused = true;
    for (std::size_t n : {std::size_t(0), std::size_t(10), std::size_t(30), text.size() / 2, text.size() - 40, text.size() - 1})
        if (lic::readBlocks(text.substr(0, n), blocks, &why)) allTruncRefused = false;
    check(allTruncRefused, "enveloppe : tronquee a 0, 10, 30, la moitie, -40, -1 octet -> refusee");
    check(!lic::readBlocks(text.substr(0, text.size() / 2), blocks, &why) && has(why, "tronqu"), "enveloppe tronquee : la raison le dit (" + why + ")");
    std::string unknown = text;
    unknown.replace(unknown.find("revocations"), 11, "flottantes!");
    check(!lic::readBlocks(unknown, blocks, &why) && has(why, "inconnu"), "enveloppe : genre inconnu refuse");
    std::string crlf = text;
    crlf.replace(crlf.find("{\"x\": 1}\n"), 9, "{\"x\": 1}\r\n");
    check(!lic::readBlocks(crlf, blocks, &why), "enveloppe : fins de ligne changees (CRLF) -> refusee");
    check(!lic::readBlocks(text + "texte en trop", blocks, &why), "enveloppe : texte en trop apres le bloc -> refuse");
    // Le contexte de signature : une signature de revocations ne vaut pas pour une licence.
    lic::SignedBlock other = b;
    other.kind = lic::DocKind::License;
    check(!lic::verifyBlock(other, g_key->publicKey()), "la signature est liee au genre du document (un registre ne passe pas pour une licence)");
}

// ============================================================ la cle de l'editeur ==
void testPublisherKey() {
    std::printf("-- la cle de l'editeur\n");
    check(g_key && g_key->test() && has(g_key->name(), adm::kTestKeyMarker), "cle d'essai creee, marquee comme telle dans son nom");
    std::string why;
    check(!adm::createPublisherKey(U(g_root / "admin"), "", true, &why) && has(why, "existe"), "une deuxieme cle n'ecrase pas la premiere");
    auto again = adm::loadPublisherKey(U(g_root / "admin"), &why);
    check(again && again->publicKey() == g_key->publicKey(), "la cle se relit, la meme");
    const std::string file = readFile(adm::publisherKeyPath(U(g_root / "admin")));
    check(has(file, adm::kAdminMarker), "le fichier de la cle porte la marque de l'Admin (le controle du paquet la trouve)");
    check(!has(file, deploy::crypto::hex(g_key->publicKey()).substr(0, 8) + "secrete"), "fichier de cle : la partie secrete n'est pas la cle publique");
    check(!adm::createPublisherKey(U(g_root / "admin-x"), std::string("Faux ") + adm::kTestKeyMarker, false, &why), "une vraie cle ne peut pas porter la marque d'essai");
    const std::string header = adm::publisherKeyHeader(*g_key, g_now);
    check(has(header, "CLE D'ESSAI") && has(header, adm::kTestKeyMarker) && has(header, ", true,") && has(header, g_key->keyId())
              && has(header, "kTestKeys") && !has(header, "kKeys") && has(header, "#ifdef XPG_LICENCES_CLE_ESSAI"),
          "cle d'essai exportee : au format de TestPublisherKey.hpp (kTestKeys, sous XPG_LICENCES_CLE_ESSAI), test = true");
    check(isAscii(header), "le C++ genere : ASCII seulement");

    // 1.30.0 : les deux fichiers du depot, exactement ce qu'ecrit exporter-cle.
    std::string why2;
    for (const bool test : {false, true}) {
        const fs::path f = fs::path(g_repo) / "src" / "licensing" / (test ? adm::kTestPublisherHeaderName : adm::kPublisherHeaderName);
        const std::string text = readFile(f);
        std::vector<adm::ExportedKey> keys;
        bool isTest = !test;
        const bool read = adm::readPublisherKeyHeader(text, keys, isTest, &why2);
        check(read && isTest == test, U(f.filename()) + " : relu, du bon genre (" + why2 + ")");
        Seconds date = 0;
        const std::string mark = "exporter-cle) le ";
        const auto at = text.find(mark);
        if (at != std::string::npos) (void)lic::parseIsoTime(text.substr(at + mark.size(), 20), date);
        check(date != 0 && adm::publisherKeyHeader(keys, test, date) == text, U(f.filename()) + " : octet pour octet ce qu'ecrit exporter-cle (date " + lic::isoTime(date) + ")");
        check(isAscii(text), U(f.filename()) + " : ASCII seulement");
        if (test) {
            check(keys.size() == 1 && keys[0].id == "xpg-cb0d1ae560b52310" && keys[0].test && has(keys[0].name, adm::kTestKeyMarker),
                  "TestPublisherKey.hpp : la cle d'essai xpg-cb0d1ae560b52310, marquee");
        } else {
            bool real = keys.size() == lic::publisher::kKeys.size();
            for (const auto& k : keys) real = real && !k.test && !has(k.name, adm::kTestKeyMarker);
            check(real && !has(text, adm::kTestKeyMarker), "PublisherKey.hpp : les vraies cles seulement (" + std::to_string(keys.size()) + "), jamais la marque d'essai");
            if (keys.empty()) check(has(text, "AUCUNE CLE") && has(text, "pas encore actives"), "PublisherKey.hpp sans cle : l'en-tete le dit (licences pas encore actives)");
        }
    }
    // Ce que ce programme croit : les vraies cles, plus la cle d'essai seulement avec l'option.
    const auto compiled = lic::compiledPublisherKeys();
    std::size_t tests = 0;
    for (const auto& k : compiled) tests += k.test ? 1 : 0;
    bool idsOk = true;
    for (const auto& k : compiled) idsOk = idsOk && k.id == lic::keyIdOf(k.key);
    // (la macro n'est definie que pour xpg_licensing : testKeyCompiled() dit comment il a ete construit)
    if (lic::testKeyCompiled())
        check(tests == 1 && compiled.size() == lic::publisher::kKeys.size() + 1, "XPG_LICENCES_CLE_ESSAI : la cle d'essai est compilee, en plus des vraies");
    else
        check(tests == 0 && compiled.size() == lic::publisher::kKeys.size(), "sans XPG_LICENCES_CLE_ESSAI : aucune cle d'essai compilee");
    check(idsOk, "cles compilees : chaque identifiant est celui de sa cle");

    // Le format a plusieurs vraies cles (une rotation) : des cles publiques tirees au hasard (aucune cle
    // privee n'est creee ici), ecrites puis relues.
    std::vector<adm::ExportedKey> many;
    for (int i = 0; i < 3; ++i) {
        adm::ExportedKey k;
        deploy::crypto::randomBytes(k.key.data(), k.key.size());
        k.id = lic::keyIdOf(k.key);
        k.name = std::string("Cl\xC3\xA9" "a de format ") + std::to_string(i + 1) + " \"essai\" \xC3\xA9" "1";   // accents suivis d'un chiffre hexadecimal, guillemets
        many.push_back(k);
    }
    const std::string three = adm::publisherKeyHeader(many, false, g_now);
    std::vector<adm::ExportedKey> back;
    bool backTest = true;
    check(adm::readPublisherKeyHeader(three, back, backTest, &why) && !backTest && back.size() == 3 && back[1].id == many[1].id && back[2].key == many[2].key
              && back[0].name == many[0].name && isAscii(three) && has(three, "std::array<KeyEntry, 3> kKeys{{"),
          "PublisherKey.hpp a 3 cles : ecrit en ASCII, relu a l'identique (noms accentues compris) (" + why + ")");
    const std::string none = adm::publisherKeyHeader(std::vector<adm::ExportedKey>{}, false, g_now);
    check(adm::readPublisherKeyHeader(none, back, backTest, &why) && back.empty() && !backTest && has(none, "std::array<KeyEntry, 0> kKeys{};"),
          "PublisherKey.hpp sans cle : une liste vide (std::array de 0), relue");
    std::string bad = three;
    char& digit = bad[bad.find("0x", bad.find("kKeys{{")) + 3];   // un octet d'une cle change : l'identifiant ne va plus
    digit = digit == '0' ? '1' : '0';
    check(!adm::readPublisherKeyHeader(bad, back, backTest, &why) && has(why, "identifiant"), "une cle modifiee a la main : refusee a la relecture (" + why + ")");
    // exporter-cle : les genres ne se melangent jamais.
    std::string out, summary;
    const bool refused = !adm::exportPublisherKeys(g_key.get(), "", "src/licensing/PublisherKey.hpp", false, false, g_now, out, &why, &summary);
    check(refused && has(why, "ESSAI"), "exporter-cle : une cle d'essai vers PublisherKey.hpp -> refuse (" + why + ")");
    check(adm::exportPublisherKeys(g_key.get(), "", "src/licensing/TestPublisherKey.hpp", false, false, g_now, out, &why, &summary) && has(out, "kTestKeys"),
          "exporter-cle : une cle d'essai vers TestPublisherKey.hpp -> ecrite");
    check(!adm::exportPublisherKeys(g_key.get(), three, "cles.hpp", true, false, g_now, out, &why, &summary) && has(why, "vraies"),
          "exporter-cle --garder : une cle d'essai ne se garde pas a cote de vraies cles");
    check(adm::exportPublisherKeys(nullptr, three, "PublisherKey.hpp", false, true, g_now, out, &why, &summary) && out == adm::publisherKeyHeader(std::vector<adm::ExportedKey>{}, false, g_now),
          "exporter-cle --vide : le fichier sans cle");
    check(!adm::exportPublisherKeys(nullptr, "", "TestPublisherKey.hpp", false, true, g_now, out, &why, &summary), "exporter-cle --vide vers TestPublisherKey.hpp : refuse");
}

// =================================================================== le registre ==
lic::Registry g_reg;

void testRegistry() {
    std::printf("-- le registre\n");
    const std::string input = readFile(fs::path(g_repo) / "tests" / "fixtures" / "licences" / "registre-exemple.json");
    std::string why;
    check(!input.empty(), "registre d'exemple lu (tests/fixtures/licences)");
    check(adm::signRegistry(*g_key, input, g_now, g_registry, &why), "registre signe (" + why + ")");
    std::vector<lic::SignedBlock> blocks;
    check(lic::readBlocks(g_registry, blocks, &why) && blocks.size() == 1 && lic::parseRegistry(blocks[0].document, g_reg, &why), "registre signe : relu");
    check(g_reg.number == 318 && g_reg.unlicensedEdition == "FREE" && g_reg.features.size() == 21 && g_reg.editions.size() == 6,
          "registre n. 318 : 21 fonctionnalites, 6 editions, sans licence : FREE");
    const auto count = [&](const char* e) { return g_reg.edition(e) ? g_reg.edition(e)->features.size() : 999u; };
    check(count("FREE") == 4 && count("PERSONAL") == 10 && count("PROFESSIONAL") == 18 && count("ENTERPRISE") == 21 && count("EVALUATION") == 18
              && count("CUSTOM") == 0,
          "les editions de la maquette : 4, 10, 18, 21, 18, 0 fonctionnalites");
    // Ecrire est stable : relire puis reecrire donne les memes octets.
    check(lic::writeRegistry(g_reg) == blocks[0].document, "registre : l'ecriture est canonique (relu puis reecrit : les memes octets)");
    // Les refus de l'Admin.
    std::string out;
    check(!adm::signRegistry(*g_key, "{\"number\": 2, \"number\": 3, \"unlicensed_edition\": \"FREE\", \"features\": [], \"editions\": []}", g_now, out, &why)
              && has(why, "deux fois"),
          "registre a signer : champ en double refuse");
    check(!adm::signRegistry(*g_key,
                             "{\"number\": 2, \"unlicensed_edition\": \"FREE\", \"features\": [], \"editions\": [{\"id\": \"FREE\", \"label\": \"F\", \"type\": "
                             "\"perpetual\", \"max_devices\": 1, \"duration_days\": 0, \"offline_grace_days\": 0, \"features\": [{\"id\": \"ai_x\"}]}]}",
                             g_now, out, &why)
              && has(why, "ai_x"),
          "registre a signer : une edition qui accorde une fonctionnalite non declaree -> refuse");
    check(!adm::signRegistry(*g_key, "{\"number\": 2, \"unlicensed_edition\": \"GRATUIT\", \"features\": [], \"editions\": []}", g_now, out, &why)
              && has(why, "GRATUIT"),
          "registre a signer : l'edition sans licence doit exister");
}

// ======================================================== un PC, de bout en bout ==
void testOnePc() {
    std::printf("-- un PC : sans licence, demande, licence, grace, fin, horloge\n");
    const fs::path pc = g_root / "pc1";
    lic::StoreOptions o = optionsFor(pc);
    lic::LicenseStore store(o);
    g_now = T("2026-11-06T10:00:00Z");

    // Sans rien.
    auto s = store.evaluate();
    check(s.status == LicenseStatus::Absent && s.edition.empty() && s.features.empty() && has(s.message, "registre"),
          "rien sur ce PC : Absente, aucune fonctionnalite, la phrase dit que le registre manque");
    // Le registre seul : FREE.
    auto rep = store.importBytes(g_registry);
    check(rep.ok && rep.state.status == LicenseStatus::Absent && rep.state.edition == "FREE", "registre importe : Absente, edition FREE");
    check(rep.state.hasFeature("api_editor") && rep.state.hasFeature("hmi_runtime") && !rep.state.hasFeature("api_compiler")
              && !rep.state.hasFeature("ai_generation") && rep.state.maxDevices == 1,
          "FREE sans licence : api_editor, hmi_runtime oui ; api_compiler, ai_generation non");
    check(has(rep.state.message, "FREE"), "FREE sans licence : la phrase le dit (" + rep.state.message + ")");
    rep = store.importBytes(g_registry);
    check(rep.ok && !rep.lines.empty() && has(rep.lines[0], "jour"), "le meme registre reimporte : deja a jour");

    // La demande d'activation : la cle du PC, et rien du materiel.
    const std::string req = requestOf(pc, "Poste essai 1", "XPG-ESSAI-0001");
    std::vector<lic::SignedBlock> blocks;
    std::string why;
    lic::ActivationRequest q;
    check(lic::readBlocks(req, blocks, &why) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::Request && lic::parseRequest(blocks[0].document, q, &why),
          "demande d'activation ecrite et relue");
    check(lic::isKeyHex(q.deviceKey) && q.deviceName == "Poste essai 1" && q.licenseId == "XPG-ESSAI-0001", "demande : la cle publique du PC, le nom choisi");
#ifndef _WIN32
    char host[256] = {};
    if (::gethostname(host, sizeof host - 1) == 0 && std::string(host).size() >= 4)
        check(!has(req, host), "demande : le nom reseau du PC n'y est pas");
#endif
    check(!has(req, "mac") && !has(req, "serial") && !has(req, "hostname"), "demande : ni MAC, ni numero de serie, ni nom de machine (aucun champ de materiel)");
    const std::string keyFile = readFile(pc / lic::kDeviceKeyFile);
    check(has(keyFile, "format=xpg-identite-pc/1") && has(keyFile, "publique=" + q.deviceKey)
              && has(keyFile, deploy::crypto::protectedHere() ? "protection=dpapi-machine" : "protection=fichier"),
          "la cle du PC est rangee (DPAPI machine sous Windows, 0600 ailleurs)");
#ifndef _WIN32
    std::error_code ec;
    const auto perms = fs::status(pc / lic::kDeviceKeyFile, ec).permissions();
    check((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none, "la cle du PC : lisible par son seul proprietaire (0600)");
#endif

    // La licence : PROFESSIONAL, 2 PC, un an, grace 14 jours.
    const std::string licFile = signLicense(params("PROFESSIONAL", "\"max_devices\": 2, \"expires_at\": \"2027-11-06T00:00:00Z\", \"offline_grace_days\": 14"), {req});
    check(!licFile.empty(), "licence signee par l'Admin");
    check(lic::readBlocks(licFile, blocks, &why) && blocks.size() == 2, "le fichier de licence porte aussi le registre (un seul fichier a importer)");
    rep = store.importBytes(licFile);
    check(rep.ok && rep.licenseInstalled && rep.state.status == LicenseStatus::Valid, "licence importee : Valide (" + rep.why + ")");
    s = rep.state;
    check(s.edition == "PROFESSIONAL" && s.licenseId == "XPG-ESSAI-0001" && s.maxDevices == 2 && s.boundDevices == 1 && s.expirationDate() == "2027-11-06"
              && s.customer == "Client d'essai",
          "l'etat : edition, identifiant, PC, fin, client");
    check(s.hasFeature("ai_generation") && s.hasFeature("api_compiler") && !s.hasFeature("ai_advanced_models") && !s.hasFeature("multi_user"),
          "PROFESSIONAL : ai_generation, api_compiler oui ; ai_advanced_models, multi_user non");
    check(has(s.message, "valide jusqu'au 2027-11-06"), "Valide : la phrase (" + s.message + ")");
    check(!s.deviceFingerprint.empty(), "l'empreinte de la cle de ce PC est donnee (pour l'ecran)");
    // 1.30.0 : l'historique de ce PC (le plus recent d'abord).
    auto h = store.history();
    check(h.size() == 6 && h[0].kind == "etat" && h[0].ok && has(h[0].text, "Absente \xE2\x86\x92 Valide") && h[1].kind == "import" && h[1].ok
              && has(h[1].text, "XPG-ESSAI-0001") && h[2].kind == "demande" && h[2].ok && has(h[2].text, "Poste essai 1") && h[3].kind == "import"
              && has(h[3].text, "jour") && h[4].kind == "import" && h[5].kind == "etat" && has(h[5].text, "Absente") && h[1].at == g_now,
          "historique : etat constate, registre importe (puis deja a jour), demande ecrite, licence importee, Absente -> Valide (" + std::to_string(h.size())
              + " evenements)");

    // L'API de la maquette.
    lic::initialize(o);
    check(lic::isFeatureEnabled("ai_generation") && !lic::isFeatureEnabled("ai_advanced_models") && lic::getEdition() == "PROFESSIONAL"
              && lic::getLicenseStatus() == LicenseStatus::Valid && lic::getMaxDevices() == 2 && lic::getExpirationDate() == "2027-11-06"
              && lic::getLicenseId() == "XPG-ESSAI-0001" && lic::getStatusLabel() == "Valide",
          "API : isFeatureEnabled, getEdition, getLicenseStatus, getMaxDevices, getExpirationDate, getLicenseId");
    check(has(lic::lockedMessage("ai_advanced_models"), "PROFESSIONAL") && has(lic::lockedMessage("ai_advanced_models"), "Enregistrer"),
          "API : la phrase d'une entree verrouillee (" + lic::lockedMessage("ai_advanced_models") + ")");
    check(!lic::getFeatureLimit("ai_generation", "requests_per_day").has_value(), "API : pas de limite fixee -> aucune");

    // L'heure avance : la grace, puis la fin.
    g_now = T("2027-11-09T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::Grace && s.graceDaysLeft == 11 && s.hasFeature("ai_generation") && s.edition == "PROFESSIONAL",
          "3 jours apres la fin : Grace, 11 jours restent, les fonctionnalites restent");
    check(has(s.message, "11 jours") && has(s.message, "enregistrables"), "Grace : la phrase (" + s.message + ")");
    auto last = lic::getHistory(1);
    check(last.size() == 1 && last[0].kind == "etat" && last[0].ok && has(last[0].text, "Valide \xE2\x86\x92 Gr\xC3\xA2" "ce") && last[0].at == g_now,
          "historique : le changement d'etat constate par refresh (Valide -> Grace)");
    g_now = T("2027-11-21T00:00:01Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::Expired && s.edition == "FREE" && !s.hasFeature("ai_generation") && s.hasFeature("api_editor")
              && s.licensedEdition == "PROFESSIONAL",
          "au-dela de la grace : Expiree, edition FREE, ai_generation ferme, api_editor ouvert");
    check(has(s.message, "Enregistrer et Exporter restent permis") && has(s.message, "FREE"), "Expiree : jamais de perte de travail, dit (" + s.message + ")");
    last = lic::getHistory(0);
    check(!last.empty() && last[0].kind == "etat" && !last[0].ok && has(last[0].text, "Gr\xC3\xA2" "ce \xE2\x86\x92 Expir\xC3\xA9" "e") && last.size() == 8,
          "historique : Grace -> Expiree (une anomalie : ok faux), 8 evenements");
    s = lic::refresh();
    check(lic::getHistory(0).size() == 8, "historique : un refresh sans changement n'ajoute rien");
    check(!lic::isFeatureEnabled("ai_generation") && lic::getEdition() == "FREE" && lic::getLicenseStatus() == LicenseStatus::Expired,
          "API : Expiree -> FREE");

    // L'horloge recule (l'heure la plus tardive vue : 2027-11-21).
    g_now = T("2027-10-01T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::ClockRolledBack && s.edition == "FREE" && !s.hasFeature("ai_generation"),
          "horloge reculee (de 51 jours) : Horloge reculee, FREE - pas de retour a Valide");
    check(has(s.message, "remets-la") && has(s.message, "Enregistrer et Exporter"), "Horloge reculee : la phrase (" + s.message + ")");
    g_now = T("2027-11-20T12:00:00Z");
    s = lic::refresh();
    check(s.status != LicenseStatus::ClockRolledBack, "un recul de moins de 24 h est tolere");
    // 1.32.0 (M1) : l'etat protege efface, une licence installee : il est RECREE, ancre sur l'historique (sa ligne la
    // plus tardive : 2027-11-21) - effacer etat.bin ne remet plus l'horloge a zero.
    fs::remove(pc / lic::LicenseStore::kStateFile);
    g_now = T("2027-01-15T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::ClockRolledBack && has(s.detail, "recr"),
          "etat efface + horloge reculee : Horloge reculee, l'etat recree ancre sur l'historique (" + s.detail + ")");
    last = lic::getHistory(0);
    check(std::any_of(last.begin(), last.end(), [](const lic::HistoryEvent& e) { return e.kind == "controle" && !e.ok && has(e.text, "recr\xC3\xA9\xC3\xA9"); }),
          "historique : la ligne << Etat recree >> (controle)");
    // L'etat ET l'historique effaces : les dates des documents signes gardent un ancrage.
    fs::remove(pc / lic::LicenseStore::kStateFile);
    fs::remove(pc / lic::LicenseStore::kHistoryFile);
    g_now = T("2026-10-01T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::ClockRolledBack, "etat et historique effaces + horloge avant l'emission de la licence : Horloge reculee (ancrage des documents)");
    // Remise a l'heure.
    g_now = T("2027-01-15T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::Valid, "horloge remise a l'heure : Valide de nouveau");
    lic::resetForTests();
    check(!lic::isFeatureEnabled("api_editor") && lic::getLicenseStatus() == LicenseStatus::Absent && lic::getEdition().empty(),
          "API avant initialize : Absente, rien d'ouvert");

    // Une emission plus ancienne de la meme licence ne remplace pas la plus recente.
    g_now = T("2027-01-15T01:00:00Z");
    const std::string older = signLicense(params("PROFESSIONAL", "\"max_devices\": 2, \"issued_at\": \"2026-10-01T00:00:00Z\", \"expires_at\": \"2027-12-01T00:00:00Z\""), {req});
    rep = store.importBytes(older);
    check(!rep.ok && has(rep.why, "plus ancienne") && rep.state.status == LicenseStatus::Valid, "une emission plus ancienne : refusee, la licence installee reste");
    rep = store.importBytes(licFile);
    check(rep.ok && !rep.licenseInstalled && has(rep.lines.back(), "d\xC3\xA9" "j\xC3\xA0 install"), "la meme licence reimportee : deja installee");
}

// =================================================================== les refus ==
void install(const fs::path& pc, const std::string& licenseText) { writeFile(pc / lic::LicenseStore::kLicenseFile, licenseText); }

void testRefusals() {
    std::printf("-- les refus\n");
    g_now = T("2027-01-15T00:00:00Z");
    const fs::path pc1 = g_root / "pc1";
    const fs::path pc2 = g_root / "pc2";
    lic::LicenseStore s1(optionsFor(pc1));
    lic::LicenseStore s2(optionsFor(pc2));
    (void)s2.importBytes(g_registry);
    const std::string req1 = requestOf(pc1, "Poste essai 1");
    const std::string req2 = requestOf(pc2, "Poste essai 2");
    const std::string good = readFile(pc1 / lic::LicenseStore::kLicenseFile);
    std::vector<lic::SignedBlock> blocks;
    std::string why;
    check(lic::readBlocks(good, blocks, &why) && blocks.size() == 1, "la licence installee de pc1 : un bloc");
    const std::string doc = blocks[0].document;

    // Autre PC.
    auto rep = s2.importFile(U(pc1 / lic::LicenseStore::kLicenseFile));
    check(!rep.ok && has(rep.why, "autre PC") && rep.state.status == LicenseStatus::Absent, "la licence de pc1 importee sur pc2 : refusee (autre PC)");
    install(pc2, good);
    auto st = s2.evaluate();
    check(st.status == LicenseStatus::OtherDevice && st.edition == "FREE" && !st.hasFeature("ai_generation") && has(st.message, "autre PC"),
          "la licence de pc1 copiee sur pc2 : Autre PC, FREE (" + st.message + ")");
    fs::remove(pc2 / lic::LicenseStore::kLicenseFile);

    // Signature fausse : un octet change dans le JSON (la taille reste).
    std::string forged = good;
    const auto at = forged.find("\"max_devices\": 2");
    forged[at + 15] = '9';
    install(pc1, forged);
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "fausse") && st.edition == "FREE", "max_devices change de 2 a 9 : Invalide (signature fausse)");
    check(has(st.message, "Enregistrer et Exporter restent permis"), "Invalide : jamais de perte de travail, dit");
    // Signe par une autre cle d'editeur.
    std::string w;
    auto otherKey = adm::createPublisherKey(U(g_root / "admin-autre"), "", true, &w);
    install(pc1, signRaw(lic::DocKind::License, doc, *otherKey));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "inconnue"), "signee par une autre cle : Invalide (cle inconnue)");
    // Tronquee.
    install(pc1, good.substr(0, good.size() / 2));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "tronqu"), "fichier tronque : Invalide (" + st.detail + ")");
    rep = s1.importBytes(good.substr(0, good.size() - 10));
    check(!rep.ok && has(rep.why, "tronqu"), "import d'un fichier tronque : refuse");
    // Champ en double (signe quand meme : le verifieur le refuse).
    std::string dup = doc;
    dup.insert(dup.find("  \"edition\""), "  \"edition\": \"ENTERPRISE\",\n");
    install(pc1, signRaw(lic::DocKind::License, dup, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "deux fois"), "champ en double (edition) : Invalide (" + st.detail + ")");
    // Champ inconnu.
    std::string unk = doc;
    unk.insert(unk.find("  \"edition\""), "  \"unlimited\": true,\n");
    install(pc1, signRaw(lic::DocKind::License, unk, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "inconnu"), "champ inconnu : Invalide (" + st.detail + ")");
    // Edition inconnue.
    std::string ed = doc;
    ed.replace(ed.find("\"PROFESSIONAL\""), 14, "\"PLATINUM\"");
    install(pc1, signRaw(lic::DocKind::License, ed, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "PLATINUM") && has(st.detail, "inconnue"), "edition inconnue du registre : Invalide (" + st.detail + ")");
    check(signLicense(params("PLATINUM"), {req1}, &why).empty() && has(why, "PLATINUM"), "l'Admin refuse une edition inconnue");
    // Fonctionnalite inconnue.
    std::string feat = doc;
    feat.replace(feat.find("\"ai_assistant\""), 14, "\"ai_telepathy\"");
    install(pc1, signRaw(lic::DocKind::License, feat, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "ai_telepathy"), "fonctionnalite inconnue du registre : Invalide");
    // Flottante, service en ligne : prevus dans le format, refuses en version 1.
    std::string fl = doc;
    fl.replace(fl.find("\"temporary\""), 11, "\"floating\"");
    install(pc1, signRaw(lic::DocKind::License, fl, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "flottante"), "licence flottante : lue, puis refusee (pas en version 1)");
    check(signLicense(params("ENTERPRISE", "\"type\": \"floating\""), {req1}, &why).empty() && has(why, "flottantes"), "l'Admin refuse une licence flottante");
    std::string sv = doc;
    sv.replace(sv.find("\"service\": null"), 15, "\"service\": \"https://licences.example\"");
    install(pc1, signRaw(lic::DocKind::License, sv, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "service en ligne"), "licence liee a un service en ligne : refusee (pas en version 1)");
    // Plus de PC lies que permis.
    std::string many = doc;
    many.replace(many.find("\"max_devices\": 2"), 16, "\"max_devices\": 0");
    install(pc1, signRaw(lic::DocKind::License, many, *g_key));
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid, "max_devices 0 : Invalide");
    install(pc1, good);
    check(s1.evaluate().status == LicenseStatus::Valid, "la bonne licence remise : Valide");

    // Le registre plus ancien.
    std::string reg317;
    std::string input = readFile(fs::path(g_repo) / "tests" / "fixtures" / "licences" / "registre-exemple.json");
    input.replace(input.find("318"), 3, "317");
    check(adm::signRegistry(*g_key, input, g_now, reg317, &why), "registre n. 317 signe");
    rep = s1.importBytes(reg317);
    check(!rep.ok && has(rep.why, "plus ancien") && rep.state.status == LicenseStatus::Valid, "registre plus ancien (317 apres 318) : refuse");
    writeFile(pc1 / lic::LicenseStore::kRegistryFile, reg317);
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.registryProblem, "plus ancien"), "registre remplace sur le disque par le 317 : refuse, licence Invalide");
    writeFile(pc1 / lic::LicenseStore::kRegistryFile, g_registry);
    check(s1.evaluate().status == LicenseStatus::Valid, "le registre 318 remis : Valide");
    // Un registre plus recent livre avec le programme est adopte.
    std::string reg319;
    input.replace(input.find("317"), 3, "319");
    (void)adm::signRegistry(*g_key, input, g_now, reg319, &why);
    writeFile(g_root / "livre" / "registre.xpgreg", reg319);
    lic::StoreOptions o = optionsFor(pc1);
    o.bundledRegistry = U(g_root / "livre" / "registre.xpgreg");
    st = lic::LicenseStore(o).evaluate();
    check(st.status == LicenseStatus::Valid && st.registryNumber == 319, "le registre livre avec le programme (319) est adopte");
    st = s1.evaluate();
    check(st.registryNumber == 319, "...et garde dans le magasin du PC");

    // Les revocations.
    const std::string rev1 = revocations(1, "{\"license_id\": \"XPG-AUTRE-0099\", \"reason\": \"Fin de contrat\"}");
    rep = s1.importBytes(rev1);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.revocationNumber == 1, "liste n. 1 (une autre licence) : Valide");
    (void)lic::readBlocks(req2, blocks, &why);
    lic::ActivationRequest q2;
    (void)lic::parseRequest(blocks[0].document, q2, &why);
    const std::string rev2 = revocations(2, "{\"license_id\": \"XPG-ESSAI-0001\", \"device\": \"" + q2.deviceKey + "\", \"reason\": \"Transfert\"}");
    rep = s1.importBytes(rev2);
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "liste n. 2 : la licence retiree pour un AUTRE PC seulement -> ce PC reste Valide");
    const std::string rev3 = revocations(3, "{\"license_id\": \"XPG-ESSAI-0001\", \"reason\": \"Fin de contrat\"}");
    rep = s1.importBytes(rev3);
    check(rep.ok && rep.state.status == LicenseStatus::Revoked && rep.state.edition == "FREE" && !rep.state.hasFeature("ai_generation"),
          "liste n. 3 : licence revoquee -> Revoquee, FREE");
    check(has(rep.state.message, "Fin de contrat") && has(rep.state.message, "Enregistrer et Exporter restent permis"), "Revoquee : la phrase (" + rep.state.message + ")");
    rep = s1.importBytes(rev2);
    check(!rep.ok && has(rep.why, "plus ancienne"), "liste plus ancienne (2 apres 3) : refusee");
    writeFile(pc1 / lic::LicenseStore::kRevocationsFile, rev2);
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "remplac"), "liste remplacee sur le disque par la n. 2 : Invalide, pas Valide (" + st.detail + ")");
    fs::remove(pc1 / lic::LicenseStore::kRevocationsFile);
    st = s1.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "disparu"), "liste effacee du disque : Invalide, pas Valide");
    rep = s1.importBytes(rev3);
    check(rep.ok && rep.state.status == LicenseStatus::Revoked, "la liste n. 3 reimportee : Revoquee");
    rep = s1.importBytes(good);
    check(!rep.ok && has(rep.why, "r\xC3\xA9voqu"), "la licence revoquee ne se reinstalle pas");
}

// ======================================================== les editions, les PC ==
void testEditions() {
    std::printf("-- les fonctionnalites par edition, plusieurs PC\n");
    g_now = T("2027-02-01T00:00:00Z");
    const fs::path pc = g_root / "pc3";
    lic::LicenseStore s(optionsFor(pc));
    const std::string req = requestOf(pc, "Poste essai 3");
    std::string why;
    for (const auto& e : g_reg.editions) {
        if (e.id == "FREE") continue;
        const std::string licFile = signLicense(params(e.id, "", "XPG-ED-" + e.id), {req}, &why);
        if (licFile.empty()) {
            check(false, "edition " + e.id + " : signee (" + why + ")");
            continue;
        }
        auto rep = s.importBytes(licFile);
        bool same = rep.state.features.size() == e.features.size();
        for (const auto& g : e.features) same = same && rep.state.hasFeature(g.id);
        check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.edition == e.id && same,
              "edition " + e.id + " : Valide, exactement les " + std::to_string(e.features.size()) + " fonctionnalites du registre");
        if (e.id == "PERSONAL")
            check(rep.state.hasFeature("ai_assistant") && !rep.state.hasFeature("api_export") && !rep.state.hasFeature("ai_generation") && rep.state.maxDevices == 2,
                  "PERSONAL (maquette) : ai_assistant oui, api_export et ai_generation non, 2 PC");
        if (e.id == "ENTERPRISE")
            check(rep.state.hasFeature("multi_user") && rep.state.hasFeature("ai_advanced_models") && rep.state.maxDevices == 20, "ENTERPRISE : tout, 20 PC");
        if (e.id == "EVALUATION") {
            check(rep.state.type == lic::LicenseType::Evaluation && rep.state.expirationDate() == "2027-03-03", "EVALUATION : 30 jours");
            g_now = T("2027-03-03T00:00:01Z");
            auto st = s.evaluate();
            check(st.status == LicenseStatus::Expired && st.graceEndsAt == 0, "EVALUATION : finie -> Expiree tout de suite, sans grace");
        }
        if (e.id == "CUSTOM") check(rep.state.features.empty() && rep.state.status == LicenseStatus::Valid, "CUSTOM : aucune fonctionnalite par defaut");
    }
    // Une liste choisie et des limites.
    auto lf = signLicense(params("CUSTOM", "\"features\": [\"api_editor\", {\"id\": \"ai_generation\", \"limits\": {\"requests_per_day\": 200}}, {\"id\": \"hmi_editor\", \"limits\": {\"max_views\": 50}}]", "XPG-ED-CUSTOM2"), {req}, &why);
    auto rep = s.importBytes(lf);
    check(rep.ok && rep.state.hasFeature("ai_generation") && rep.state.limit("ai_generation", "requests_per_day") == 200
              && rep.state.limit("hmi_editor", "max_views") == 50 && !rep.state.limit("api_editor", "max_views"),
          "CUSTOM choisie : ai_generation (200 par jour), hmi_editor (50 vues), api_editor");
    check(signLicense(params("CUSTOM", "\"features\": [{\"id\": \"api_editor\", \"limits\": {\"max_views\": 3}}]", "XPG-X"), {req}, &why).empty(),
          "une limite non declaree pour cette fonctionnalite : l'Admin refuse");
    // Perpetuelle : l'horloge ne compte pas.
    lf = signLicense(params("PROFESSIONAL", "\"type\": \"perpetual\"", "XPG-PERP-1"), {req}, &why);
    rep = s.importBytes(lf);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.expiresAt == 0 && rep.state.expirationDate().empty() && has(rep.state.message, "perp"),
          "perpetuelle : Valide, sans fin (" + rep.state.message + ")");
    const Seconds keep = g_now;
    g_now = T("2026-01-01T00:00:00Z");
    check(s.evaluate().status == LicenseStatus::Valid, "perpetuelle : l'horloge qui recule ne la ferme pas");
    g_now = keep;

    // Plusieurs PC : etendre une licence, la limite de PC.
    const fs::path pc4 = g_root / "pc4", pc5 = g_root / "pc5";
    const std::string req4 = requestOf(pc4, "Poste essai 4"), req5 = requestOf(pc5, "Poste essai 5");
    const std::string first = signLicense(params("PERSONAL", "", "XPG-MULTI-1"), {req}, &why);
    const std::string both = signLicense(params("PERSONAL", "", "XPG-MULTI-1"), {req4}, &why, first);
    std::vector<lic::SignedBlock> blocks;
    lic::License l;
    check(!both.empty() && lic::readBlocks(both, blocks, &why) && lic::parseLicense(blocks[0].document, l, &why) && l.devices.size() == 2,
          "etendre la licence : les PC deja lies sont gardes, le nouveau s'ajoute (2 sur 2)");
    lic::LicenseStore s4(optionsFor(pc4));
    rep = s4.importBytes(both);
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.boundDevices == 2, "le 2e PC importe la licence etendue : Valide");
    const bool third = signLicense(params("PERSONAL", "", "XPG-MULTI-1"), {req5}, &why, both).empty();
    check(third && has(why, "3 PC pour 2"), "un 3e PC sur PERSONAL (2 PC) : refuse (" + why + ")");
    check(signLicense(params("PERSONAL", "", "XPG-AUTRE-2"), {req}, &why, first).empty() && has(why, "XPG-MULTI-1"),
          "etendre avec un autre identifiant : refuse");
    // La demande modifiee en chemin, ou pour une autre licence.
    std::string bad = req5;
    bad.replace(bad.find("Poste essai 5"), 13, "Poste essai X");
    check(signLicense(params("PERSONAL", "", "XPG-M2"), {bad}, &why).empty() && has(why, "pas sign"), "demande modifiee en chemin : l'Admin refuse");
    const std::string reqFor = requestOf(g_root / "pc6", "Poste essai 6", "XPG-AUTRE-9");
    check(signLicense(params("PERSONAL", "", "XPG-M3"), {reqFor}, &why).empty() && has(why, "XPG-AUTRE-9"), "demande pour une autre licence : l'Admin refuse");
    check(signLicense(params("PERSONAL"), {}, &why).empty() && has(why, "aucun PC"), "aucune demande : l'Admin refuse");
    // Une revocation jointe au fichier : l'Admin refuse de signer une licence revoquee.
    const std::string rv = revocations(9, "{\"license_id\": \"XPG-MULTI-1\", \"reason\": \"Essai\"}");
    check(signLicense(params("PERSONAL", "", "XPG-MULTI-1"), {req5}, &why, {}, rv).empty() && has(why, "r\xC3\xA9vocation"),
          "l'Admin refuse de signer une licence deja revoquee");
}

// ============================================ 1.30.0 : les licences pas encore activees ==
void testNotConfigured() {
    std::printf("-- un programme sans cle de l'editeur : licences non activees\n");
    const fs::path pc = g_root / "pc-sans-cle";
    lic::StoreOptions o;
    o.folder = U(pc);
    o.compiledKeys = false;   // comme un programme compile avec un PublisherKey.hpp vide
    o.clock = [] { return g_now; };
    lic::LicenseStore store(o);
    auto s = store.evaluate();
    check(!store.configured() && s.status == LicenseStatus::NotConfigured && s.edition.empty() && s.features.empty(), "aucune cle : NotConfigured, aucune edition");
    check(s.hasFeature("ai_generation") && s.hasFeature("une_fonctionnalite_inconnue"), "NotConfigured : toutes les fonctionnalites sont ouvertes (meme inconnues)");
    check(s.message == "Les licences ne sont pas encore actives dans ce programme : il ne contient pas encore la cl\xC3\xA9 de l'\xC3\xA9" "diteur. Toutes les fonctionnalit\xC3\xA9s sont ouvertes.",
          "NotConfigured : la phrase (" + s.message + ")");
    check(std::string(lic::statusKey(s.status)) == "non-configuree" && lic::statusLabel(s.status) == "Non activ\xC3\xA9" "es", "NotConfigured : non-configuree, << Non activees >>");
    std::error_code ec;
    check(!fs::exists(pc, ec), "NotConfigured : l'evaluation n'ecrit rien (pas meme le dossier)");

    lic::initialize(o);
    check(!lic::isConfigured() && lic::getLicenseStatus() == LicenseStatus::NotConfigured && lic::isFeatureEnabled("ai_generation")
              && lic::isFeatureEnabled("multi_user") && lic::getEdition().empty() && lic::getStatusLabel() == "Non activ\xC3\xA9" "es",
          "API sans cle : isConfigured faux, isFeatureEnabled toujours vrai, getEdition vide");
    check(has(lic::lockedMessage("ai_generation"), "ouverte") && has(lic::getStatusMessage(), "pas encore actives"), "API sans cle : lockedMessage et getStatusMessage le disent");
    check(!lic::getRegistry().has_value() && lic::getLicensedDevices().empty() && lic::thisDeviceFingerprint().empty() && lic::getLicenseFolder() == U(pc),
          "API sans cle : pas de registre, pas de PC lies, pas encore d'empreinte, le dossier");
    // L'import : refuse, avec la phrase.
    const fs::path licFile = g_root / "pc4" / lic::LicenseStore::kLicenseFile;
    auto rep = lic::importLicenseFile(U(licFile));
    check(!rep.ok && rep.why == "Ce programme ne peut pas encore v\xC3\xA9rifier une licence : il ne contient pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur ; mets-le \xC3\xA0 jour."
              && rep.state.status == LicenseStatus::NotConfigured && !fs::exists(pc / lic::LicenseStore::kLicenseFile, ec),
          "NotConfigured : l'import est refuse (" + rep.why + "), rien d'installe");
    // La demande : elle marche (elle ne depend pas de la cle de l'editeur).
    std::string why;
    const fs::path req = g_root / "pc-sans-cle.xpgreq";
    check(lic::writeActivationRequest(U(req), "Poste essai sans cle", "", "IHM Studio (essai)", &why) && fs::is_regular_file(req, ec), "NotConfigured : la demande d'activation s'ecrit (" + why + ")");
    std::vector<lic::SignedBlock> blocks;
    lic::ActivationRequest q;
    check(lic::readBlocks(readFile(req), blocks, &why) && blocks.size() == 1 && lic::parseRequest(blocks[0].document, q, &why) && q.deviceName == "Poste essai sans cle",
          "NotConfigured : la demande se relit");
    check(!lic::thisDeviceFingerprint().empty() && lic::getLicenseStatus() == LicenseStatus::NotConfigured && lic::isFeatureEnabled("ai_generation"),
          "NotConfigured : apres la demande, l'empreinte de ce PC ; toujours tout ouvert");
    const auto h = lic::getHistory(0);
    check(h.size() == 2 && h[0].kind == "demande" && h[0].ok && has(h[0].text, "Poste essai sans cle") && h[1].kind == "import" && !h[1].ok
              && has(h[1].text, "ne peut pas encore"),
          "NotConfigured : l'historique - la demande, l'import refuse (et aucun changement d'etat note)");
    lic::resetForTests();
    // Le programme de cet essai : ses cles compilees (PublisherKey.hpp, et la cle d'essai avec l'option).
    lic::StoreOptions c = o;
    c.compiledKeys = true;
    const bool none = lic::compiledPublisherKeys().empty();
    lic::LicenseStore compiled(c);
    check(compiled.configured() == !none && (compiled.evaluate().status == LicenseStatus::NotConfigured) == none,
          std::string("ce programme : ") + (none ? "aucune cle compilee -> NotConfigured" : "des cles compilees -> les licences sont actives"));
}

// ================================================ 1.30.0 : l'ecran Licence ==
void testScreen() {
    std::printf("-- l'ecran Licence : registre, PC lies, historique, dossier\n");
    g_now = T("2027-03-10T00:00:00Z");   // apres l'emission des licences de testEditions
    const fs::path pc4 = g_root / "pc4";
    lic::initialize(optionsFor(pc4));
    check(lic::isConfigured() && lic::getLicenseStatus() == LicenseStatus::Valid, "pc4 : Valide (la licence PERSONAL a deux PC)");
    const auto reg = lic::getRegistry();
    check(reg && reg->number == 318 && reg->unlicensedEdition == "FREE" && reg->features.size() == 21 && reg->editions.size() == 6 && reg->issuedAt.size() == 20
              && reg->issuedAt.back() == 'Z',
          "getRegistry : n. 318, 21 fonctionnalites, 6 editions, sans licence FREE, emis le " + (reg ? reg->issuedAt : std::string("?")));
    if (reg) {
        const auto f = std::find_if(reg->features.begin(), reg->features.end(), [](const lic::RegistryFeatureView& x) { return x.id == "ai_generation"; });
        check(f != reg->features.end() && f->product == "IA" && f->label == "G\xC3\xA9n\xC3\xA9ration" && f->limits.size() == 1
                  && f->limits[0].first == "requests_per_day" && f->limits[0].second == "G\xC3\xA9n\xC3\xA9rations par jour" && !f->retired,
              "getRegistry : ai_generation (IA, Generation, sa limite et son libelle)");
        const auto e = std::find_if(reg->editions.begin(), reg->editions.end(), [](const lic::RegistryEditionView& x) { return x.id == "PROFESSIONAL"; });
        check(e != reg->editions.end() && e->type == "temporary" && e->maxDevices == 5 && e->durationDays == 365 && e->graceDays == 14 && e->features.size() == 18
                  && std::is_sorted(e->features.begin(), e->features.end()),
              "getRegistry : PROFESSIONAL (temporary, 5 PC, 365 jours, 14 de grace, 18 fonctionnalites triees)");
        const auto fr = std::find_if(reg->editions.begin(), reg->editions.end(), [](const lic::RegistryEditionView& x) { return x.id == "FREE"; });
        check(fr != reg->editions.end() && fr->type == "perpetual" && fr->features.size() == 4, "getRegistry : FREE (perpetual, 4 fonctionnalites)");
    }
    const auto devices = lic::getLicensedDevices();
    const auto mine = std::count_if(devices.begin(), devices.end(), [](const lic::DeviceView& d) { return d.thisPc; });
    check(devices.size() == 2 && mine == 1, "getLicensedDevices : 2 PC lies, un seul est ce PC");
    for (const auto& d : devices) {
        if (d.thisPc)
            check(d.name == "Poste essai 4" && d.fingerprint == lic::thisDeviceFingerprint() && !d.fingerprint.empty() && d.activatedAt.size() == 20,
                  "ce PC : son nom, son empreinte (" + d.fingerprint + ") = thisDeviceFingerprint, activee le " + d.activatedAt);
        else
            check(d.name == "Poste essai 3" && d.fingerprint != lic::thisDeviceFingerprint(), "l'autre PC : son nom, une autre empreinte");
    }
    check(lic::getLicenseFolder() == U(pc4), "getLicenseFolder : le dossier de ce PC");
    // La licence d'un autre PC : les PC lies restent lisibles, aucun n'est ce PC.
    const fs::path pc5 = g_root / "pc5";
    std::error_code ec;
    fs::copy_file(pc4 / lic::LicenseStore::kLicenseFile, pc5 / lic::LicenseStore::kLicenseFile, fs::copy_options::overwrite_existing, ec);
    fs::copy_file(pc4 / lic::LicenseStore::kRegistryFile, pc5 / lic::LicenseStore::kRegistryFile, fs::copy_options::overwrite_existing, ec);
    lic::initialize(optionsFor(pc5));
    const auto other = lic::getLicensedDevices();
    check(lic::getLicenseStatus() == LicenseStatus::OtherDevice && other.size() == 2 && std::none_of(other.begin(), other.end(), [](const lic::DeviceView& d) { return d.thisPc; }),
          "la licence d'autres PC copiee ici : Autre PC, ses 2 PC listes, aucun n'est ce PC");
    // Pas de licence lisible : aucun PC.
    fs::remove(pc5 / lic::LicenseStore::kLicenseFile, ec);
    lic::refresh();
    check(lic::getLicensedDevices().empty() && lic::getRegistry().has_value(), "sans licence : aucun PC lie ; le registre reste");

    // L'historique borne : 500 lignes, les plus anciennes partent ; les lignes illisibles sont ignorees.
    lic::LicenseStore st(optionsFor(pc4));
    std::string why;
    bool added = true;
    for (int i = 0; i < 600; ++i) {
        lic::HistoryEvent e;
        e.at = g_now + i;
        e.kind = "import";
        e.ok = i % 2 == 0;
        e.text = "\xC3\xA9v\xC3\xA9nement " + std::to_string(i);
        added = st.addHistory(e, "", &why) && added;
    }
    auto all = st.history();
    check(added && all.size() == 500 && all.front().text == "\xC3\xA9v\xC3\xA9nement 599" && all.back().text == "\xC3\xA9v\xC3\xA9nement 100" && all.front().at == g_now + 599
              && !all.front().ok && all[1].ok,
          "historique : 600 ajouts -> 500 lignes, du 599 (le plus recent d'abord) au 100 ; heures et ok relus");
    {
        std::ofstream out(pc4 / lic::LicenseStore::kHistoryFile, std::ios::binary | std::ios::app);
        out << "pas du JSON\n{\"kind\": \"import\", \"text\": \"sans heure\"}\n[1, 2]\n\n{\"at\": \"2027-02-01T00:00:00Z\", \"kind\": \"etat\", \"ok\": true, \"status\": \"valid\", \"text\": \"lisible\", \"plus_tard\": 1}\n";
    }
    all = st.history();
    check(all.size() == 501 && all.front().text == "lisible" && all.front().kind == "etat", "historique : 3 lignes illisibles ignorees, une ligne d'une version plus recente lue");
    check(st.history(3).size() == 3 && st.history(3)[0].text == "lisible", "historique : la limite");
    lic::HistoryEvent one;
    one.at = g_now;
    one.kind = "demande";
    one.ok = true;
    one.text = "une de plus";
    check(st.addHistory(one, "", &why) && st.history().size() == 500, "historique : a l'ajout suivant, les illisibles partent et la borne tient (500)");
    const std::string raw = readFile(pc4 / lic::LicenseStore::kHistoryFile);
    check(std::count(raw.begin(), raw.end(), '\n') == 500 && !has(raw, "pas du JSON") && !has(raw, "sans heure"), "historique : 500 lignes JSON dans le fichier, une par evenement");
    lic::initialize(optionsFor(pc4));
    check(lic::getHistory(0).size() == 500 && lic::getHistory(2).size() == 2 && lic::getHistory(1)[0].text == "une de plus", "getHistory : relu a initialize, le plus recent d'abord");

    // Un dossier ou ce compte ne peut pas ecrire : l'evaluation marche, detail le dit.
    const fs::path ro = g_root / "pc-lecture";
    lic::LicenseStore w(optionsFor(ro));
    (void)w.importBytes(g_registry);
    fs::remove(ro / lic::LicenseStore::kHistoryFile, ec);
    fs::remove(ro / lic::LicenseStore::kStateFile, ec);
    // a la place des fichiers, des dossiers non vides : ils ne se remplacent plus (comme un dossier en lecture seule)
    writeFile(ro / lic::LicenseStore::kHistoryFile / "occupe.txt", "x");
    writeFile(ro / lic::LicenseStore::kStateFile / "occupe.txt", "x");
    lic::LicenseStore second(optionsFor(ro));                            // un autre programme (un autre compte)
    auto s = second.evaluate();
    check(s.status == LicenseStatus::Absent && s.edition == "FREE" && s.hasFeature("api_editor") && has(s.detail, "ne peut pas \xC3\xA9" "crire")
              && has(s.detail, "historique") && has(s.detail, U(ro)),
          "dossier non inscriptible : l'evaluation marche (FREE), detail le dit (" + s.detail + ")");
    const std::string req = requestOf(g_root / "pc4", "Poste essai 4");
    auto r = second.importBytes(signLicense(params("PERSONAL", "", "XPG-LECTURE-1"), {req}, &why));
    check(!r.ok, "dossier non inscriptible : l'import d'une licence d'un autre PC est refuse sans planter");
    lic::resetForTests();
}

// ================================ 1.30.0 : la marque de la cle d'essai dans la bibliotheque ==
void testTestKeyMarker(const std::string& library) {
    std::printf("-- la marque de la cle d'essai dans la bibliotheque (%s)\n", library.c_str());
    std::error_code ec;
    if (library.empty() || !fs::is_regular_file(library, ec)) {
        check(false, "la bibliotheque xpg_licensing est donnee et existe (" + library + ")");
        return;
    }
    const std::string bytes = readFile(library);
    const bool marker = has(bytes, adm::kTestKeyMarker);
    const bool keyId = has(bytes, "xpg-cb0d1ae560b52310");
    if (lic::testKeyCompiled())
        check(marker && keyId, "construite AVEC XPG_LICENCES_CLE_ESSAI : la cle d'essai y est (et le controle du paquet la refusera)");
    else
        check(!marker && !keyId, "construite sans XPG_LICENCES_CLE_ESSAI : ni la marque de la cle d'essai, ni son identifiant (ce qui lie xpg_licensing ne l'a pas)");
    check(!has(bytes, adm::kAdminMarker) && !has(bytes, "XPG-License-Admin"), "la bibliotheque des clients : rien de l'outil Admin");
    // 1.32.0 (M1) : la lecture de XPG_LICENCES_DOSSIER n'est compilee qu'avec la cle d'essai.
    check(has(bytes, "XPG_LICENCES_DOSSIER") == lic::testKeyCompiled(),
          std::string("XPG_LICENCES_DOSSIER ") + (lic::testKeyCompiled() ? "lu (construction d'essai)" : "absent de la bibliotheque (construction normale)"));
}

// ======================================= 1.32.0 (H2) : l'empreinte du poste ==
// Le premier bloc d'un fichier signe (la licence d'un .xpglic).
std::string firstBlock(const std::string& file) {
    std::vector<lic::SignedBlock> blocks;
    std::string why;
    if (!lic::readBlocks(file, blocks, &why) || blocks.empty()) return {};
    return lic::writeBlock(blocks[0]);
}

lic::License licenseOf(const std::string& file) {
    std::vector<lic::SignedBlock> blocks;
    std::string why;
    lic::License l;
    if (lic::readBlocks(file, blocks, &why) && !blocks.empty()) (void)lic::parseLicense(blocks[0].document, l, &why);
    return l;
}

void testStation() {
    std::printf("-- 1.32.0 : l'empreinte du poste (une cle de PC recopiee sur une autre installation)\n");
    g_now = T("2027-04-01T00:00:00Z");
    const fs::path pcA = g_root / "poste-a", pcB = g_root / "poste-b";
    lic::StoreOptions oa = optionsFor(pcA);
    oa.machineId = "machine-essai-a";
    lic::StoreOptions ob = optionsFor(pcB);
    ob.machineId = "machine-essai-b";
    lic::LicenseStore sa(oa);
    (void)sa.importBytes(g_registry);
    std::string why;
    const fs::path reqPath = g_root / "poste-a.xpgreq";
    check(sa.writeActivationRequest(U(reqPath), "Poste essai A", "", "IHM Studio (essai)", &why), "la demande du poste A (" + why + ")");
    const std::string req = readFile(reqPath);
    std::vector<lic::SignedBlock> blocks;
    lic::ActivationRequest q;
    check(lic::readBlocks(req, blocks, &why) && blocks.size() == 1 && lic::parseRequest(blocks[0].document, q, &why) && lic::isStationHex(q.station),
          "la demande porte l'empreinte du poste (32 hexadecimaux) : " + q.station);
    deploy::crypto::Bytes raw;
    deploy::crypto::PublicKey pub{};
    if (deploy::crypto::unhex(q.deviceKey, raw, 32)) std::copy(raw.begin(), raw.end(), pub.begin());
    deploy::crypto::PublicKey other = pub;
    other[0] ^= 1;
    check(q.station == lic::stationHash("machine-essai-a", pub) && lic::stationHash("machine-essai-b", pub) != q.station
              && lic::stationHash("machine-essai-a", other) != q.station && lic::stationHash("", pub).empty(),
          "stationHash : propre a l'installation ET a la cle du PC (salee) ; sans identifiant : vide");
    check(!has(req, "machine-essai-a"), "la demande ne porte jamais l'identifiant de l'installation (seulement son empreinte salee)");
    check(lic::systemMachineId() == lic::LicenseStore(optionsFor(g_root / "pc-sysid")).machineId(), "sans machineId donne : celui du systeme");

    const std::string licFile = signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-04-01T00:00:00Z\"", "XPG-POSTE-0001"), {req});
    const lic::License l = licenseOf(licFile);
    check(l.devices.size() == 1 && l.devices[0].station == q.station, "l'Admin recopie l'empreinte du poste dans le PC lie de la licence");
    auto rep = sa.importBytes(licFile);
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "sur l'installation qui a fait la demande : Valide (" + rep.why + ")");

    // La cle du PC (et tout le dossier) recopiee sur une autre installation de Windows.
    std::error_code ec;
    fs::create_directories(pcB, ec);
    fs::copy(pcA, pcB, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    auto st = lic::LicenseStore(ob).evaluate();
    check(st.status == LicenseStatus::OtherDevice && has(st.detail, "installation") && !st.hasFeature("ai_generation"),
          "la cle du PC recopiee ailleurs : Autre PC (" + st.detail + ")");
    rep = lic::LicenseStore(ob).importBytes(licFile);
    check(!rep.ok && has(rep.why, "installation"), "... et l'import y est refuse (" + rep.why + ")");
    // Depuis la copie, une demande avec la meme cle : l'Admin refuse de l'ajouter a la licence.
    lic::LicenseStore sb(ob);
    const fs::path reqB = g_root / "poste-b.xpgreq";
    check(sb.writeActivationRequest(U(reqB), "Poste essai B", "XPG-POSTE-0001", "IHM Studio (essai)", &why), "la demande depuis la copie");
    check(signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-04-01T00:00:00Z\"", "XPG-POSTE-0001"), {readFile(reqB)}, &why, licFile).empty()
              && has(why, "AUTRE installation"),
          "l'Admin : la meme cle depuis une autre installation -> refuse (" + why + ")");
    check(lic::LicenseStore(oa).evaluate().status == LicenseStatus::Valid, "l'installation d'origine reste Valide");
    // Une licence d'avant la 1.32.0 (sans empreinte) : rien a comparer, et son texte n'a pas le champ.
    lic::License old = l;
    old.devices[0].station.clear();
    const std::string oldText = lic::writeLicense(old);
    check(!has(oldText, "station"), "un PC lie sans empreinte : le champ n'est pas ecrit (la 1.31 relit la licence)");
    writeFile(pcB / lic::LicenseStore::kLicenseFile, signRaw(lic::DocKind::License, oldText, *g_key));
    check(lic::LicenseStore(ob).evaluate().status == LicenseStatus::Valid, "une licence sans empreinte : pas de controle (Valide)");
    // Une empreinte mal ecrite : la licence est refusee a la lecture.
    lic::License badStation = l;
    badStation.devices[0].station = "XYZ";
    lic::License back;
    check(!lic::parseLicense(lic::writeLicense(badStation), back, &why) && has(why, "station"), "une empreinte qui n'a pas 32 hexadecimaux : refusee (" + why + ")");
}

// ======================= 1.32.0 (M2) : les emissions d'une licence ==
void testIssues() {
    std::printf("-- 1.32.0 : les emissions d'une licence (en revoquer une, ne jamais revenir a une plus ancienne)\n");
    g_now = T("2027-05-01T00:00:00Z");
    const fs::path pc = g_root / "pc-emissions";
    lic::LicenseStore s(optionsFor(pc));
    (void)s.importBytes(g_registry);
    const std::string req = requestOf(pc, "Poste essai emissions");
    const std::string v1 = signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-05-01T00:00:00Z\"", "XPG-EMIS-0001"), {req});
    auto rep = s.importBytes(v1);
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "emission 1 (2027-05-01) : Valide");
    g_now = T("2027-05-10T00:00:00Z");
    const std::string v2 = signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-05-10T00:00:00Z\"", "XPG-EMIS-0001"), {req});
    rep = s.importBytes(v2);
    check(rep.ok && rep.licenseInstalled && rep.state.status == LicenseStatus::Valid, "emission 2 (2027-05-10) : installee, Valide");
    // Le vieux fichier remis a la main a la place du nouveau : refuse a l'evaluation.
    writeFile(pc / lic::LicenseStore::kLicenseFile, firstBlock(v1));
    auto st = s.evaluate();
    check(st.status == LicenseStatus::Invalid && has(st.detail, "plus ancienne") && !st.hasFeature("ai_generation"),
          "l'emission 1 remise sur le disque : Invalide (" + st.detail + ")");
    rep = s.importBytes(v1);
    check(!rep.ok && has(rep.why, "plus ancienne"), "l'emission 1 importee : refusee (" + rep.why + ")");
    rep = s.importBytes(v2);
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "l'emission 2 reimportee : Valide");
    // Une autre licence puis le retour a celle-ci : la meme emission revient (pas plus ancienne).
    const std::string otherId = signLicense(params("PERSONAL", "", "XPG-EMIS-0009"), {req});
    rep = s.importBytes(otherId);
    check(rep.ok && rep.state.licenseId == "XPG-EMIS-0009", "une autre licence installee");
    rep = s.importBytes(v2);
    check(rep.ok && rep.state.licenseId == "XPG-EMIS-0001" && rep.state.status == LicenseStatus::Valid, "retour a l'emission 2 : permis (la meme)");

    // issued_before : revoquer les emissions signees AVANT une date (l'emission 1), pas la 2.
    const std::string rv = revocations(20, "{\"license_id\": \"XPG-EMIS-0001\", \"reason\": \"Emission remplacee\", \"issued_before\": \"2027-05-10T00:00:00Z\"}");
    std::vector<lic::SignedBlock> blocks;
    lic::RevocationList list;
    std::string why;
    check(lic::readBlocks(rv, blocks, &why) && lic::parseRevocations(blocks[0].document, list, &why) && list.revoked.size() == 1
              && list.revoked[0].issuedBefore == T("2027-05-10T00:00:00Z"),
          "signer-revocations accepte issued_before (" + why + ")");
    check(list.find("XPG-EMIS-0001", "", T("2027-05-01T00:00:00Z")) && !list.find("XPG-EMIS-0001", "", T("2027-05-10T00:00:00Z")),
          "find : l'emission du 05-01 est revoquee, celle du 05-10 non");
    rep = s.importBytes(rv);
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "la liste importee : l'emission 2 reste Valide");
    lic::RevocationList plain;
    plain.number = 1;
    plain.issuedAt = g_now;
    plain.revoked.push_back({"XPG-X", "", g_now, "essai", 0});
    check(!has(lic::writeRevocations(plain), "issued_before"), "une revocation sans issued_before : le champ n'est pas ecrit (la 1.31 relit la liste)");
    std::string out;
    check(!adm::signRevocations(*g_key, "{\"number\": 21, \"revoked\": [{\"license_id\": \"XPG-X\", \"reason\": \"r\", \"issued_before\": \"hier\"}]}", g_now, out, &why)
              && has(why, "issued_before"),
          "issued_before qui n'est pas une date : refuse (" + why + ")");

    // De bout en bout sur un autre PC : l'emission 1 revoquee -> Revoquee ; l'Admin signe la nouvelle, Valide.
    g_now = T("2027-05-01T00:00:00Z");
    const fs::path pc2 = g_root / "pc-emissions-2";
    lic::LicenseStore s2(optionsFor(pc2));
    (void)s2.importBytes(g_registry);
    const std::string req2 = requestOf(pc2, "Poste essai emissions 2");
    rep = s2.importBytes(signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-05-01T00:00:00Z\"", "XPG-EMIS-0002"), {req2}));
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "pc2 : emission du 2027-05-01, Valide");
    g_now = T("2027-05-12T00:00:00Z");
    const std::string rv2 = revocations(21, "{\"license_id\": \"XPG-EMIS-0002\", \"reason\": \"Copie\", \"issued_before\": \"2027-05-05T00:00:00Z\"}");
    rep = s2.importBytes(rv2);
    check(rep.ok && rep.state.status == LicenseStatus::Revoked, "pc2 : les emissions d'avant le 2027-05-05 revoquees -> Revoquee");
    check(signLicense(params("PROFESSIONAL", "\"issued_at\": \"2027-05-01T00:00:00Z\", \"expires_at\": \"2028-05-01T00:00:00Z\"", "XPG-EMIS-0002"), {req2}, &why, {}, rv2).empty()
              && has(why, "r\xC3\xA9vocation"),
          "l'Admin refuse une emission que la liste jointe revoque");
    const std::string fresh = signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-05-12T00:00:00Z\"", "XPG-EMIS-0002"), {req2}, &why, {}, rv2);
    rep = s2.importBytes(fresh);
    check(!fresh.empty() && rep.ok && rep.state.status == LicenseStatus::Valid, "la nouvelle emission (apres la date) : signee et Valide (" + rep.why + ")");

    // Le registre des activations de l'Admin : une revocation d'emission ne touche pas un PC deja re-emis.
    const std::string key(64, 'a');
    std::vector<adm::ActivationRecord> recs;
    adm::ActivationRecord a;
    a.kind = "licence";
    a.licenseId = "XPG-K";
    a.deviceKey = key;
    a.at = a.signedAt = a.issuedAt = T("2027-05-01T00:00:00Z");
    recs.push_back(a);
    a.at = a.signedAt = a.issuedAt = T("2027-05-10T00:00:00Z");
    recs.push_back(a);
    adm::ActivationRecord r;
    r.kind = "revocation";
    r.licenseId = "XPG-K";
    r.at = r.revokedAt = T("2027-05-11T00:00:00Z");
    r.issuedBefore = T("2027-05-10T00:00:00Z");
    recs.push_back(r);
    auto known = adm::knownDevices(recs, T("2027-05-12T00:00:00Z"));
    check(known.size() == 1 && known[0].state == "active", "liste : l'emission d'avant revoquee, le PC re-emis reste actif");
    recs.back().issuedBefore = T("2027-05-11T00:00:00Z");
    known = adm::knownDevices(recs, T("2027-05-12T00:00:00Z"));
    check(known.size() == 1 && known[0].state == "revoque", "liste : la revocation couvre aussi la derniere emission -> revoque");
    const std::string dir = U(g_root / "admin-emissions");
    std::vector<adm::ActivationRecord> again;
    int skipped = 0;
    check(adm::appendActivations(dir, recs, &why) && adm::readActivations(dir, again, &skipped, &why) && again.size() == 3
              && again[1].issuedAt == T("2027-05-10T00:00:00Z") && again[2].issuedBefore == T("2027-05-11T00:00:00Z"),
          "activations.jsonl : issued_at et issued_before relus");
}

// ================ 1.32.0 (M1) : l'etat protege, l'horloge, les documents livres ==
void testStateRebuild() {
    std::printf("-- 1.32.0 : l'etat protege efface ou illisible, la liste de revocation livree, XPG_LICENCES_DOSSIER\n");
    g_now = T("2027-06-01T00:00:00Z");
    const fs::path pc = g_root / "pc-etat";
    Seconds fileClock = 0;
    lic::StoreOptions o = optionsFor(pc);
    o.fileTime = [&fileClock](const std::string&) { return fileClock; };   // l'heure des fichiers, donnee par l'essai
    lic::LicenseStore s(o);
    (void)s.importBytes(g_registry);
    const std::string req = requestOf(pc, "Poste essai etat");
    auto rep = s.importBytes(signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-06-01T00:00:00Z\"", "XPG-ETAT-0001"), {req}));
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "pc-etat : Valide");
    const auto controles = [&] {
        int n = 0;
        for (const auto& e : s.history()) n += e.kind == "controle" && has(e.text, "recr\xC3\xA9\xC3\xA9") ? 1 : 0;
        return n;
    };
    check(controles() == 0, "rien n'a ete recree");
    // L'etat et l'historique effaces, l'horloge reculee : les fichiers du dossier (leur heure) gardent l'ancrage.
    std::error_code ec;
    fs::remove(pc / lic::LicenseStore::kStateFile, ec);
    fs::remove(pc / lic::LicenseStore::kHistoryFile, ec);
    fileClock = T("2027-09-01T00:00:00Z");
    g_now = T("2027-07-01T00:00:00Z");
    auto st = s.evaluate();
    check(st.status == LicenseStatus::ClockRolledBack && has(st.detail, "2027-09-01"), "etat et historique effaces : ancre sur l'heure des fichiers (" + st.detail + ")");
    check(controles() == 1, "historique : une ligne << Etat recree >>");
    st = s.evaluate();
    check(controles() == 1, "l'etat recree est ecrit : la ligne n'est pas repetee");
    fileClock = 0;
    g_now = T("2027-09-02T00:00:00Z");
    check(s.evaluate().status == LicenseStatus::Valid, "horloge remise a l'heure : Valide");
    // L'etat seul efface : l'historique (sa derniere ligne : 2027-09-02) garde l'ancrage.
    fs::remove(pc / lic::LicenseStore::kStateFile, ec);
    g_now = T("2027-08-01T00:00:00Z");
    st = s.evaluate();
    check(st.status == LicenseStatus::ClockRolledBack, "etat seul efface : ancre sur l'historique (" + st.detail + ")");
    // L'etat abime (illisible) : de meme.
    writeFile(pc / lic::LicenseStore::kStateFile, "abime");
    st = s.evaluate();
    check(st.status == LicenseStatus::ClockRolledBack && has(st.detail, "recr"), "etat illisible : recree et ancre, pas remis a zero (" + st.detail + ")");
    // Un import juste apres l'effacement n'ecrit pas un etat neuf a la place.
    fs::remove(pc / lic::LicenseStore::kStateFile, ec);
    rep = s.importBytes(g_registry);
    check(rep.state.status == LicenseStatus::ClockRolledBack, "etat efface puis un import : l'heure la plus tardive tient toujours");
    // Sans licence : rien a recreer (un PC neuf).
    const fs::path fresh = g_root / "pc-etat-neuf";
    lic::LicenseStore n(optionsFor(fresh));
    (void)n.importBytes(g_registry);
    fs::remove(fresh / lic::LicenseStore::kStateFile, ec);
    (void)n.evaluate();
    bool none = true;
    for (const auto& e : n.history()) none = none && e.kind != "controle";
    check(none, "sans licence : un etat absent n'est pas une anomalie (pas de ligne)");

    // La liste de revocation livree avec le programme : adoptee si elle est plus recente, sa date releve l'horloge.
    g_now = T("2027-07-01T00:00:00Z");
    const fs::path pr = g_root / "pc-livre-rev";
    lic::LicenseStore sr(optionsFor(pr));
    (void)sr.importBytes(g_registry);
    const std::string reqR = requestOf(pr, "Poste essai livre");
    rep = sr.importBytes(signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-07-01T00:00:00Z\"", "XPG-LIVRE-0001"), {reqR}));
    check(rep.ok && rep.state.status == LicenseStatus::Valid, "pc-livre-rev : Valide");
    g_now = T("2027-07-02T00:00:00Z");
    writeFile(g_root / "livre-rev" / "revocations.xpgrev", revocations(30, "{\"license_id\": \"XPG-LIVRE-0001\", \"reason\": \"Livree\"}"));
    lic::StoreOptions orv = optionsFor(pr);
    orv.bundledRevocations = U(g_root / "livre-rev" / "revocations.xpgrev");
    st = lic::LicenseStore(orv).evaluate();
    check(st.status == LicenseStatus::Revoked && st.revocationNumber == 30, "la liste livree (n. 30) est adoptee : Revoquee");
    st = sr.evaluate();
    check(st.status == LicenseStatus::Revoked && st.revocationNumber == 30, "...et gardee dans le magasin du PC");
    writeFile(g_root / "livre-rev-1" / "revocations.xpgrev", revocations(1, "{\"license_id\": \"XPG-AUTRE-0001\", \"reason\": \"Vieille\"}"));
    orv.bundledRevocations = U(g_root / "livre-rev-1" / "revocations.xpgrev");
    check(lic::LicenseStore(orv).evaluate().revocationNumber == 30, "une liste livree plus ancienne (n. 1) : ignoree");
    // Sa date d'emission releve l'heure la plus tardive (une liste signee le 2027-10-01, l'horloge au 2027-08-01).
    const fs::path pr2 = g_root / "pc-livre-rev-2";
    lic::LicenseStore sr2(optionsFor(pr2));
    (void)sr2.importBytes(g_registry);
    rep = sr2.importBytes(signLicense(params("PROFESSIONAL", "\"expires_at\": \"2028-07-01T00:00:00Z\"", "XPG-LIVRE-0002"), {requestOf(pr2, "Poste essai livre 2")}));
    g_now = T("2027-10-01T00:00:00Z");
    writeFile(g_root / "livre-rev-2" / "revocations.xpgrev", revocations(31, "{\"license_id\": \"XPG-AUTRE-0002\", \"reason\": \"Autre\"}"));
    g_now = T("2027-08-01T00:00:00Z");
    check(sr2.evaluate().status == LicenseStatus::Valid, "pc-livre-rev-2 : Valide au 2027-08-01 sans la liste livree");
    lic::StoreOptions orv2 = optionsFor(pr2);
    orv2.bundledRevocations = U(g_root / "livre-rev-2" / "revocations.xpgrev");
    st = lic::LicenseStore(orv2).evaluate();
    check(st.status == LicenseStatus::ClockRolledBack, "la liste livree signee le 2027-10-01 : l'horloge au 2027-08-01 est reculee (" + st.detail + ")");

    // XPG_LICENCES_DOSSIER : lu SEULEMENT dans une construction d'essai (XPG_LICENCES_CLE_ESSAI).
    const std::string folder = U(g_root / "dossier-variable");
#ifdef _WIN32
    _putenv_s("XPG_LICENCES_DOSSIER", folder.c_str());
#else
    setenv("XPG_LICENCES_DOSSIER", folder.c_str(), 1);
#endif
    if (lic::testKeyCompiled())
        check(lic::testFolderOverride() == folder, "construction d'essai : XPG_LICENCES_DOSSIER remplace le dossier des licences");
    else
        check(lic::testFolderOverride().empty(), "construction normale : XPG_LICENCES_DOSSIER est ignore (sa lecture n'est pas compilee)");
#ifdef _WIN32
    _putenv_s("XPG_LICENCES_DOSSIER", "");
#else
    unsetenv("XPG_LICENCES_DOSSIER");
#endif
}

// ============================ 1.32.0 (M4) : le CSV de l'Admin, les noms de PC ==
void testCsvInjection() {
    std::printf("-- 1.32.0 : le CSV de l'Admin (jamais une formule), les noms de PC\n");
    std::vector<adm::KnownDevice> devs(2);
    devs[0].name = "=HYPERLINK(\"http://exemple\")";
    devs[0].customer = "+33 1 23 45";
    devs[0].licenseId = "@SOMME";
    devs[0].software = "-2+3";
    devs[0].state = "demande";
    devs[1].name = "Poste - 1";
    devs[1].customer = "\tTabulation";
    devs[1].edition = "\rRetour";
    devs[1].state = "demande";
    const std::string csv = adm::devicesCsv(devs);
    check(has(csv, "\"'=HYPERLINK(\"\"http://exemple\"\")\"") && has(csv, ";'+33 1 23 45;") && has(csv, ";'@SOMME;") && has(csv, ";'-2+3;")
              && has(csv, ";'\tTabulation;") && has(csv, "\"'\rRetour\""),
          "--csv : = + - @ tabulation et retour en tete de cellule -> une apostrophe devant (du texte pour Excel)");
    check(has(csv, "Poste - 1;") && has(csv, ";-;-;-;"), "--csv : un tiret au milieu, ou seul (pas de date), reste tel quel");
    bool noFormula = true;
    for (std::size_t i = 0; i + 1 < csv.size(); ++i)
        if ((csv[i] == ';' || csv[i] == '\n') && (csv[i + 1] == '=' || csv[i + 1] == '+' || csv[i + 1] == '@')) noFormula = false;
    check(noFormula, "--csv : aucune cellule ne commence par = + @");
    // La demande d'activation : un nom de PC qui serait une formule est refuse (a l'ecriture comme a la lecture).
    lic::LicenseStore st(optionsFor(g_root / "pc-formule"));
    std::string why;
    bool refused = true;
    for (const char* name : {"=cmd|' /C calc'!A0", "+1", "-1+1", "@SOMME(A1)"})
        refused = !st.writeActivationRequest(U(g_root / "pc-formule.xpgreq"), name, "", "IHM Studio (essai)", &why) && has(why, "formule") && refused;
    check(refused, "un nom de PC qui commence par = + - @ : la demande est refusee (" + why + ")");
    check(st.writeActivationRequest(U(g_root / "pc-formule.xpgreq"), "Poste - 1 (atelier)", "", "IHM Studio (essai)", &why), "un tiret ailleurs qu'en tete : permis");
    check(lic::looksLikeFormula("=1") && lic::looksLikeFormula("\t1") && !lic::looksLikeFormula("A=1") && !lic::looksLikeFormula(""), "looksLikeFormula");
}

// ============================================== l'outil Admin, en sous-processus ==
void testAdminTool(const std::string& admin) {
    std::printf("-- l'outil Admin en sous-processus (%s)\n", admin.c_str());
    std::error_code ec;
    if (!fs::is_regular_file(admin, ec)) {
        check(false, "l'outil Admin est construit (" + admin + ")");
        return;
    }
    const std::string dir = U(g_root / "admin-e2e");
    const fs::path work = g_root / "e2e";
    fs::create_directories(work);
    auto r = runProgram(admin, {"--version"});
    check(r.ok() && has(r.out, adm::kAdminMarker), "--version : la marque de l'Admin (" + r.summary() + ")");
    r = runProgram(admin, {});
    check(r.started && r.exitCode == 2 && has(r.out, "creer-cle"), "sans commande : l'aide, code 2");
    r = runProgram(admin, {"signer-licence", "--dossier", dir});
    check(r.exitCode == 2 && has(r.err, "--parametres"), "option manquante : code 2, la dit");
    r = runProgram(admin, {"cle", "--dossier", dir});
    check(r.exitCode == 1 && has(r.err, "creer-cle"), "pas encore de cle : refuse, dit comment la creer");
    r = runProgram(admin, {"creer-cle", "--essai", "--dossier", dir});
    check(r.ok() && has(r.out, "CL\xC3\x89 D'ESSAI"), "creer-cle --essai : creee, dite d'ESSAI (" + r.err + ")");
    r = runProgram(admin, {"creer-cle", "--essai", "--dossier", dir});
    check(r.exitCode == 1 && has(r.err, "existe"), "creer-cle deux fois : refuse, la premiere est gardee");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "PublisherKey.hpp"), "--dossier", dir});
    check(r.exitCode == 1 && has(r.err, "TestPublisherKey.hpp") && !fs::exists(work / "PublisherKey.hpp"),
          "exporter-cle : une cle d'ESSAI vers PublisherKey.hpp -> refuse, code 1, rien d'ecrit");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "TestPublisherKey.hpp"), "--dossier", dir});
    const std::string header = readFile(work / "TestPublisherKey.hpp");
    check(r.ok() && has(header, adm::kTestKeyMarker) && has(header, "namespace licensing::publisher") && has(header, "kTestKeys") && has(r.out, "XPG_LICENCES_CLE_ESSAI"),
          "exporter-cle : le C++ de la cle d'ESSAI (TestPublisherKey.hpp), l'option CMake dite");
    std::string why;
    auto key = adm::loadPublisherKey(dir, &why);
    check(key != nullptr, "la cle creee par l'outil se relit ici (" + why + ")");
    if (!key) return;
    check(has(header, key->keyId()), "le C++ porte l'identifiant de la cle");
    // Plusieurs cles (une rotation) : une deuxieme cle d'essai, --garder garde la premiere.
    const std::string dir2 = U(g_root / "admin-e2e-2");
    r = runProgram(admin, {"creer-cle", "--essai", "--nom", "rotation", "--dossier", dir2});
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "TestPublisherKey.hpp"), "--garder", "--dossier", dir2});
    std::vector<adm::ExportedKey> keys;
    bool isTest = false;
    const std::string two = readFile(work / "TestPublisherKey.hpp");
    check(r.ok() && adm::readPublisherKeyHeader(two, keys, isTest, &why) && isTest && keys.size() == 2 && keys[0].id == key->keyId() && has(r.out, "gard")
              && has(two, "std::array<KeyEntry, 2> kTestKeys{{"),
          "exporter-cle --garder : 2 cles, l'ancienne gardee, la nouvelle ajoutee (" + r.err + ")");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "TestPublisherKey.hpp"), "--garder", "--dossier", dir2});
    check(r.ok() && adm::readPublisherKeyHeader(readFile(work / "TestPublisherKey.hpp"), keys, isTest, &why) && keys.size() == 2 && has(r.out, "d\xC3\xA9j\xC3\xA0 l\xC3\xA0"),
          "exporter-cle --garder une deuxieme fois : toujours 2 cles (deja la)");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "TestPublisherKey.hpp"), "--dossier", dir2});
    check(r.ok() && adm::readPublisherKeyHeader(readFile(work / "TestPublisherKey.hpp"), keys, isTest, &why) && keys.size() == 1 && has(r.out, "retir"),
          "exporter-cle sans --garder : la cle seule ; l'autre est dite retiree");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "PublisherKey.hpp"), "--vide"});
    check(r.ok() && adm::readPublisherKeyHeader(readFile(work / "PublisherKey.hpp"), keys, isTest, &why) && keys.empty() && !isTest
              && has(readFile(work / "PublisherKey.hpp"), "AUCUNE CLE"),
          "exporter-cle --vide : PublisherKey.hpp sans cle (licences pas encore actives)");
    r = runProgram(admin, {"exporter-cle", "--sortie", U(work / "PublisherKey.hpp"), "--vide", "--garder"});
    check(r.exitCode == 2, "exporter-cle --vide --garder : mauvais usage, code 2");

    r = runProgram(admin, {"signer-registre", "--entree", U(fs::path(g_repo) / "tests/fixtures/licences/registre-exemple.json"), "--sortie",
                           U(work / "registre.xpgreg"), "--dossier", dir});
    check(r.ok() && has(r.out, "n\xC2\xB0 318"), "signer-registre : n. 318 (" + r.err + ")");

    // Le PC : sa demande.
    lic::StoreOptions o;
    o.folder = U(g_root / "pc-e2e");
    o.trustedKeys = {trustedOf(*key)};
    o.clock = [] { return g_now; };
    g_now = lic::systemNow();
    lic::LicenseStore pc(o);
    check(pc.writeActivationRequest(U(work / "pc.xpgreq"), "Poste essai E2E", "", "IHM Studio (essai)", &why), "le PC ecrit sa demande");
    r = runProgram(admin, {"verifier", U(work / "pc.xpgreq"), "--dossier", dir});
    check(r.ok() && has(r.out, "demande") && has(r.out, "bonne"), "verifier la demande : signature du PC bonne");
    writeFile(work / "p.json", "{\"license_id\": \"XPG-E2E-0001\", \"customer\": \"Client d'essai\", \"edition\": \"PROFESSIONAL\", \"max_devices\": 2, \"duration_days\": 90}");
    r = runProgram(admin, {"signer-licence", "--parametres", U(work / "p.json"), "--demande", U(work / "pc.xpgreq"), "--registre", U(work / "registre.xpgreg"),
                           "--sortie", U(work / "licence.xpglic"), "--dossier", dir});
    check(r.ok() && has(r.out, "XPG-E2E-0001") && has(r.out, "Poste essai E2E") && has(r.out, "ESSAI"), "signer-licence : signee (" + r.err + ")");
    r = runProgram(admin, {"verifier", U(work / "licence.xpglic"), "--dossier", dir});
    check(r.ok() && has(r.out, "licence") && has(r.out, "registre") && !has(r.out, "FAUSSE"), "verifier la licence : licence et registre, signatures bonnes");
    auto rep = pc.importFile(U(work / "licence.xpglic"));
    check(rep.ok && rep.state.status == LicenseStatus::Valid && rep.state.edition == "PROFESSIONAL" && rep.state.licenseId == "XPG-E2E-0001",
          "de bout en bout : demande -> licence -> import -> Valide (" + rep.why + ")");
    lic::initialize(o);
    check(lic::isFeatureEnabled("ai_generation") && !lic::isFeatureEnabled("multi_user") && lic::getMaxDevices() == 2, "de bout en bout : l'API repond");
    // Un fichier modifie : verifier le dit, l'import le refuse.
    std::string altered = readFile(work / "licence.xpglic");
    altered[altered.find("PROFESSIONAL")] = 'Q';
    writeFile(work / "altere.xpglic", altered);
    r = runProgram(admin, {"verifier", U(work / "altere.xpglic"), "--dossier", dir});
    check(r.exitCode == 1 && has(r.out, "FAUSSE"), "verifier un fichier modifie : signature FAUSSE, code 1");
    check(!pc.importFile(U(work / "altere.xpglic")).ok, "importer un fichier modifie : refuse");
    // Une demande modifiee : l'outil refuse.
    std::string req = readFile(work / "pc.xpgreq");
    req.replace(req.find("Poste essai E2E"), 15, "Poste essai E2X");
    writeFile(work / "pc-modifie.xpgreq", req);
    r = runProgram(admin, {"signer-licence", "--parametres", U(work / "p.json"), "--demande", U(work / "pc-modifie.xpgreq"), "--registre",
                           U(work / "registre.xpgreg"), "--sortie", U(work / "x.xpglic"), "--dossier", dir});
    check(r.exitCode == 1 && has(r.err, "pas sign"), "signer-licence avec une demande modifiee : refuse, code 1");
    // La revocation, de bout en bout.
    writeFile(work / "v.json", "{\"number\": 1, \"revoked\": [{\"license_id\": \"XPG-E2E-0001\", \"reason\": \"Essai de bout en bout\"}]}");
    r = runProgram(admin, {"signer-revocations", "--entree", U(work / "v.json"), "--sortie", U(work / "revocations.xpgrev"), "--dossier", dir});
    check(r.ok(), "signer-revocations : signee (" + r.err + ")");
    rep = pc.importFile(U(work / "revocations.xpgrev"));
    check(rep.ok && rep.state.status == LicenseStatus::Revoked && lic::refresh().status == LicenseStatus::Revoked && !lic::isFeatureEnabled("ai_generation")
              && lic::isFeatureEnabled("api_editor"),
          "de bout en bout : revocation importee -> Revoquee, FREE");
    lic::resetForTests();
    // Le registre a signer, abime : refus clair.
    writeFile(work / "r-bad.json", "{\"number\": 5, \"number\": 6}");
    r = runProgram(admin, {"signer-registre", "--entree", U(work / "r-bad.json"), "--sortie", U(work / "r-bad.xpgreg"), "--dossier", dir});
    check(r.exitCode == 1 && has(r.err, "deux fois") && !fs::exists(work / "r-bad.xpgreg"), "signer-registre avec un champ en double : refuse, rien d'ecrit");
    r = runProgram(admin, {"commande-inconnue"});
    check(r.exitCode == 2, "commande inconnue : code 2");
}

// ================================== 1.30.0 : le registre des activations de l'Admin ==
void testActivations(const std::string& admin) {
    std::printf("-- le registre des activations (enregistrer-demande, liste, --csv)\n");
    std::error_code ec;
    if (!fs::is_regular_file(admin, ec)) return;
    const std::string dir = U(g_root / "admin-activations");
    const fs::path work = g_root / "activations";
    fs::create_directories(work);
    auto r = runProgram(admin, {"creer-cle", "--essai", "--dossier", dir});
    r = runProgram(admin, {"liste", "--dossier", dir});
    check(r.ok() && has(r.out, "Aucun PC connu"), "liste sans rien : aucun PC connu, code 0");
    r = runProgram(admin, {"signer-registre", "--entree", U(fs::path(g_repo) / "tests/fixtures/licences/registre-exemple.json"), "--sortie", U(work / "registre.xpgreg"),
                           "--dossier", dir});
    // Quatre PC, chacun sa demande (le logiciel et sa version, comme les Studios les ecriront).
    const auto ask = [&](const char* pc, const char* name, const char* software, const std::string& licenseId = {}) {
        lic::StoreOptions o;
        o.folder = U(g_root / pc);
        o.trustedKeys = {trustedOf(*g_key)};
        o.clock = [] { return g_now; };
        lic::LicenseStore st(o);
        std::string why;
        const fs::path out = work / (std::string(pc) + ".xpgreq");
        if (!st.writeActivationRequest(U(out), name, licenseId, software, &why)) std::printf("  (demande : %s)\n", why.c_str());
        return U(out);
    };
    g_now = lic::systemNow();
    const std::string qa = ask("pc-act-a", "Poste essai A", "IHM Studio 1.30.0");
    const std::string qb = ask("pc-act-b", "Poste essai B", "API Studio 1.30.0");
    const std::string qc = ask("pc-act-c", "Poste essai C", "IHM Studio 1.30.0", "XPG-ACT-0001");
    const std::string qd = ask("pc-act-d", "Poste essai D", "API Studio 1.30.0");
    writeFile(work / "alpha.json", "{\"license_id\": \"XPG-ACT-0001\", \"customer\": \"Client Alpha\", \"edition\": \"PROFESSIONAL\", \"max_devices\": 2, \"duration_days\": 365}");
    writeFile(work / "beta.json", "{\"license_id\": \"XPG-ACT-0002\", \"customer\": \"Client B\xC3\xAAta; et fils\", \"edition\": \"PERSONAL\", "
                                  "\"issued_at\": \"2024-01-01T00:00:00Z\", \"expires_at\": \"2025-01-01T00:00:00Z\"}");
    const auto sign = [&](const std::string& params, const std::string& request, const std::string& output, const std::string& existing = {}) {
        std::vector<std::string> args = {"signer-licence", "--parametres", U(work / params), "--demande", request, "--registre", U(work / "registre.xpgreg"),
                                         "--sortie", U(work / output), "--dossier", dir};
        if (!existing.empty()) {
            args.push_back("--licence");
            args.push_back(U(work / existing));
        }
        return runProgram(admin, args);
    };
    r = sign("alpha.json", qa, "alpha.xpglic");
    check(r.ok() && has(r.out, "Registre des activations : 1 ligne"), "signer-licence (Client Alpha, PC A) : une ligne au registre (" + r.err + ")");
    const std::string first = readFile(adm::activationsPath(dir));
    r = sign("beta.json", qb, "beta.xpglic");
    check(r.ok(), "signer-licence (Client Beta, PC B, finie le 2025-01-01) (" + r.err + ")");
    r = sign("alpha.json", qc, "alpha-2.xpglic", "alpha.xpglic");
    check(r.ok() && has(r.out, "2 ligne"), "extension (--licence) : le PC C s'ajoute a la licence d'Alpha, 2 lignes (A garde, C ajoute) (" + r.err + ")");
    writeFile(work / "v.json", "{\"number\": 1, \"revoked\": [{\"license_id\": \"XPG-ACT-0001\", \"device\": \"" + [&] {
        std::vector<lic::SignedBlock> b;
        lic::ActivationRequest q;
        std::string why;
        (void)lic::readBlocks(readFile(qc), b, &why);
        (void)lic::parseRequest(b[0].document, q, &why);
        return q.deviceKey;
    }() + "\", \"reason\": \"Transfert\"}]}");
    r = runProgram(admin, {"signer-revocations", "--entree", U(work / "v.json"), "--sortie", U(work / "v.xpgrev"), "--dossier", dir});
    check(r.ok() && has(r.out, "Registre des activations : 1 ligne"), "signer-revocations (le PC C de la licence d'Alpha) : une ligne au registre");
    r = runProgram(admin, {"enregistrer-demande", qd, "--dossier", dir});
    check(r.ok() && has(r.out, "Poste essai D") && has(r.out, "API Studio 1.30.0"), "enregistrer-demande (PC D, pas encore signe) : notee (" + r.err + ")");
    std::string bad = readFile(qd);
    bad.replace(bad.find("Poste essai D"), 13, "Poste essai X");
    writeFile(work / "modifiee.xpgreq", bad);
    const std::string before = readFile(adm::activationsPath(dir));
    r = runProgram(admin, {"enregistrer-demande", U(work / "modifiee.xpgreq"), "--dossier", dir});
    check(r.exitCode == 1 && readFile(adm::activationsPath(dir)) == before, "enregistrer-demande d'une demande modifiee : refusee, code 1, rien d'ajoute");

    // Le fichier : une ligne par evenement, ajoutee a la fin (jamais reecrit), rien du materiel.
    const std::string journal = readFile(adm::activationsPath(dir));
    check(std::count(journal.begin(), journal.end(), '\n') == 6 && journal.compare(0, first.size(), first) == 0,
          "activations.jsonl : 6 lignes (A ; B ; A et C : l'extension ; la revocation de C ; la demande de D), les premieres jamais reecrites");
    check(has(journal, "\"extension\":true") && has(journal, "\"kind\":\"demande\"") && has(journal, "\"kind\":\"revocation\"") && has(journal, "IHM Studio 1.30.0")
              && has(journal, "\"requested_at\""),
          "activations.jsonl : extension, demande, revocation, le logiciel et la date de la demande");
#ifndef _WIN32
    char host[256] = {};
    if (::gethostname(host, sizeof host - 1) == 0 && std::string(host).size() >= 4) check(!has(journal, host), "activations.jsonl : pas le nom reseau du PC");
#endif
    std::vector<adm::ActivationRecord> recs;
    int skipped = -1;
    std::string why;
    check(adm::readActivations(dir, recs, &skipped, &why) && recs.size() == 6 && skipped == 0, "le registre se relit (6 evenements)");
    const auto all = adm::knownDevices(recs, g_now);
    const auto find = [&](const char* name) {
        const auto it = std::find_if(all.begin(), all.end(), [&](const adm::KnownDevice& d) { return d.name == name; });
        return it == all.end() ? adm::KnownDevice{} : *it;
    };
    const auto a = find("Poste essai A"), b = find("Poste essai B"), c = find("Poste essai C"), d = find("Poste essai D");
    check(all.size() == 4 && a.state == "active" && a.customer == "Client Alpha" && a.licenseId == "XPG-ACT-0001" && a.edition == "PROFESSIONAL" && a.software == "IHM Studio 1.30.0"
              && a.requestedAt != 0 && a.activatedAt != 0 && a.expiresAt != 0,
          "liste : A active (Client Alpha, XPG-ACT-0001, PROFESSIONAL, IHM Studio 1.30.0, demande, activation, fin)");
    check(b.state == "expire" && b.expiresAt == T("2025-01-01T00:00:00Z") && b.software == "API Studio 1.30.0", "liste : B expiree (finie le 2025-01-01)");
    check(c.state == "revoque" && c.licenseId == "XPG-ACT-0001" && c.revokedAt != 0, "liste : C revoquee (le transfert)");
    check(d.state == "demande" && d.customer.empty() && d.software == "API Studio 1.30.0" && all.back().name == "Poste essai D", "liste : D en demande, en bas du tableau");
    check(!a.fingerprint.empty() && a.fingerprint != c.fingerprint && has(a.fingerprint, ":"), "liste : l'empreinte courte de chaque PC");

    r = runProgram(admin, {"liste", "--csv", U(work / "pc.csv"), "--dossier", dir});
    check(r.ok() && has(r.out, "Poste essai A") && has(r.out, "Poste essai D") && has(r.out, "Total : 4 PC, 2 licences, 2 clients") && has(r.out, "1 activ")
              && has(r.out, "1 demande en attente") && has(r.out, "1 r\xC3\xA9voqu") && has(r.out, "1 expir"),
          "liste : les 4 PC et les totaux (4 PC, 2 licences, 2 clients ; 1 active, 1 demande, 1 revoquee, 1 expiree)");
    const std::string csv = readFile(work / "pc.csv");
    check(csv.rfind("\xEF\xBB\xBF" "PC;Empreinte;Client;Licence;", 0) == 0 && std::count(csv.begin(), csv.end(), '\n') == 5 && has(csv, "\r\n")
              && has(csv, ";\"Client B\xC3\xAAta; et fils\";") && has(csv, "Poste essai C;") && has(csv, "XPG-ACT-0002"),
          "--csv : UTF-8 avec BOM, separateur ;, CRLF, l'en-tete et 4 lignes, un champ avec ; entre guillemets");
    r = runProgram(admin, {"liste", "--client", "ALPHA", "--dossier", dir});
    check(r.ok() && has(r.out, "Poste essai A") && has(r.out, "Poste essai C") && !has(r.out, "Poste essai B") && has(r.out, "Total : 2 PC, 1 licence, 1 client"),
          "liste --client ALPHA : les PC d'Alpha seulement (sans tenir compte de la casse)");
    r = runProgram(admin, {"liste", "--licence", "XPG-ACT-0002", "--dossier", dir});
    check(r.ok() && has(r.out, "Poste essai B") && !has(r.out, "Poste essai A") && has(r.out, "Total : 1 PC"), "liste --licence XPG-ACT-0002 : le PC de cette licence");
    // Une ligne abimee : ignoree, et dite.
    {
        std::ofstream out(fs::path(adm::activationsPath(dir)), std::ios::binary | std::ios::app);
        out << "{pas du json\n";
    }
    r = runProgram(admin, {"liste", "--dossier", dir});
    check(r.ok() && has(r.out, "Total : 4 PC") && has(r.err, "1 ligne(s) illisible"), "une ligne illisible du registre : ignoree, et dite");
    r = runProgram(admin, {"enregistrer-demande", "--dossier", dir});
    check(r.exitCode == 2, "enregistrer-demande sans fichier : mauvais usage, code 2");
}

// ==================================================== le controle du paquet ==
void testPackageControl(const std::string& admin) {
    std::printf("-- le controle du paquet (package_mingw.py --controler)\n");
    const std::string python = findInPath("python3").empty() ? findInPath("python") : findInPath("python3");
    const std::string script = U(fs::path(g_repo) / "outils" / "mingw" / "package_mingw.py");
    if (python.empty()) {
        ++g_skipped;
        std::printf("  SAUTE  python3 introuvable dans le PATH : le controle du paquet n'est PAS essaye ici\n");
        return;
    }
    const fs::path st = g_root / "livraison";
    fs::remove_all(st);
    writeFile(st / "API-Studio.exe", std::string("MZ\x90\x00 programme client", 22) + "-----BEGIN PRIVATE KEY-----" + std::string(1, '\0') + "mbedtls");
    writeFile(st / "LISEZ-MOI.txt", "API Studio / IHM Studio\r\n");
    writeFile(st / "licences" / "SDL3-LICENSE.txt", "zlib\n");
    // 1.32.0 : les textes de licence des bibliotheques, tels qu'ils sont livres
    writeFile(st / "licences" / "miniz.LICENSE", "MIT\n");
    writeFile(st / "licences" / "mbedtls-LICENSE.txt", "Apache-2.0\n");
    writeFile(st / "licences" / "pdfium" / "abseil.txt", "Apache-2.0\n");
    writeFile(st / "licences" / "pdfium" / "libjpeg_turbo.ijg", "IJG\n");
    const auto control = [&](const std::vector<std::string>& extra = {}) {
        std::vector<std::string> args = {script, "--controler", U(st)};
        args.insert(args.end(), extra.begin(), extra.end());
        return runProgram(python, args, 120);
    };
    auto r = control();
    check(r.ok() && has(r.out, "rien de l'outil Admin"), "un dossier de livraison propre : accepte (et l'en-tete PEM seul de mbedTLS n'est pas une cle) (" + r.err + ")");

    struct Case {
        std::string file;
        std::string content;
        std::string what;
        std::string expect;
    };
    std::string adminBytes = readFile(admin);
    const std::string wide = [] {
        std::string w;
        for (char c : std::string("XPG-License-Admin.exe")) {
            w += c;
            w += '\0';
        }
        return w;
    }();
    const std::vector<Case> cases = {
        {"XPG-License-Admin.exe", "MZ", "l'exe de l'Admin, par son nom", "XPG-License-Admin"},
        {"outil.exe", adminBytes, "l'exe de l'Admin renomme (son nom et sa marque, dans ses octets)", "outil Admin"},
        {"aide.dll", std::string("abc") + adm::kAdminMarker + "def", "un fichier portant la marque de l'Admin", "marque"},
        {"menus.dat", "\x01\x02" + wide, "le nom de l'Admin en UTF-16 (une chaine large)", "nom de l'outil Admin"},
        {"cle.txt", "-----BEGIN PRIVATE KEY-----\nMC4CAQAwBQYDK2VwBCIEIBQ0h5N2dWZXJ5c2VjcmV0a2V5Zm9ydGVzdGluZw==\n-----END PRIVATE KEY-----\n", "une cle privee PEM",
         "cle privee"},
        {"cle-editeur-licences.cle", readFile(adm::publisherKeyPath(U(g_root / "admin"))), "le fichier de la cle de l'editeur", "cle"},
        {"identite-pc.cle", "format=xpg-identite-pc/1\n", "une cle de PC (*.cle)", "cle"},
        {"API-Studio-copie.exe", std::string("programme ") + adm::kTestKeyMarker + " fin", "un programme qui embarque la cle d'ESSAI", "ESSAI"},
        {"config.ini", "token=ghp_" + std::string(36, 'A') + "\n", "un jeton GitHub", "jeton"},
        // 1.32.0 : jamais une licence, une demande, la configuration de la synchro, l'etat ou l'historique d'un PC
        {"Client.xpglic", "XPG-LICENSING 1 licence 2\n{}", "une licence signee (*.xpglic)", "outil Admin"},
        {"poste.xpgreq", "XPG-LICENSING 1 demande 2\n{}", "une demande d'activation (*.xpgreq)", "outil Admin"},
        {"github.json", "{\"depot\": \"x/y\"}", "la configuration de la synchro GitHub", "outil Admin"},
        {"github-vu.json", "{}", "l'anti-retour de la synchro GitHub", "outil Admin"},
        {"etat.bin", "x", "l'etat protege d'un PC", "outil Admin"},
        {"historique.jsonl", "{}", "l'historique d'un PC", "outil Admin"},
        {"texte-LICENSE.txt", "XPG-LICENSING 1 licence 2\n{}", "une licence deguisee en texte (son contenu)", "licence signee"},
        {"lisez.txt", "XPG-LICENSING 1 demande 2\n{}", "une demande d'activation (son contenu)", "demande d'activation"},
    };
    for (const auto& c : cases) {
        const fs::path p = st / "sous-dossier" / c.file;
        writeFile(p, c.content);
        r = control();
        check(r.started && !r.ok() && r.exitCode != 0 && has(r.err, "construction arretee") && has(r.err, c.file) && has(r.err, c.expect),
              "livraison avec " + c.what + " : la construction s'arrete");
        if (g_verbose && r.ok()) std::printf("         sortie : %s %s\n", r.out.c_str(), r.err.c_str());
        fs::remove(p);
    }
    // 1.30.0 : le registre livre (licences/registre.xpgreg) doit etre signe par une cle des programmes.
    writeFile(st / "licences" / "registre.xpgreg", g_registry);
    r = control();
    check(r.started && !r.ok() && has(r.err, "construction arretee") && has(r.err, "registre.xpgreg") && has(r.err, "PublisherKey.hpp"),
          "un registre livre signe par une cle que les programmes ne connaissent pas : la construction s'arrete");
    writeFile(st / "licences" / "registre.xpgreg", "XPG-LICENSING 1 registre 2\n{}");
    r = control();
    check(r.started && !r.ok() && has(r.err, "pas un registre signe"), "un registre livre abime : la construction s'arrete");
    fs::remove(st / "licences" / "registre.xpgreg");
    // 1.32.0 : la liste de revocation livree, de meme ; licences\ n'admet rien d'autre ; pas d'archive de l'Admin.
    writeFile(st / "licences" / "revocations.xpgrev", revocations(40, "{\"license_id\": \"XPG-X\", \"reason\": \"essai\"}"));
    r = control();
    check(r.started && !r.ok() && has(r.err, "revocations.xpgrev") && has(r.err, "PublisherKey.hpp"),
          "une liste de revocation livree signee par une cle que les programmes ne connaissent pas : la construction s'arrete");
    writeFile(st / "licences" / "revocations.xpgrev", "XPG-LICENSING 1 revocations 2\n{}");
    r = control();
    check(r.started && !r.ok() && has(r.err, "pas une liste de revocation signee"), "une liste livree abimee : la construction s'arrete");
    fs::remove(st / "licences" / "revocations.xpgrev");
    for (const char* f : {"notes.txt", "XPG-ESSAI-0001.xpglic", "pdfium/notes.pdf", "licence-client.pdf"}) {
        writeFile(st / "licences" / fs::path(f), "x");
        r = control();
        check(r.started && !r.ok() && has(r.err, "n'admet que"), std::string("licences/") + f + " : licences\\ n'admet que le registre, la liste et les textes de licence -> arret");
        fs::remove(st / "licences" / fs::path(f));
    }
    writeFile(st / "libs" / "archive" / "licences" / "A.txt", "x");
    r = control();
    check(r.started && !r.ok() && has(r.err, "archive"), "un dossier archive (celle de l'outil Admin) : la construction s'arrete");
    fs::remove_all(st / "libs");
    // [Licences] obligatoires = oui : un paquet sans vraie cle de l'editeur est refuse.
    r = control({"--licences-obligatoires", "oui"});
    if (lic::publisher::kKeys.empty())
        check(r.started && !r.ok() && has(r.err, "obligatoires") && has(r.err, "PublisherKey.hpp"),
              "obligatoires = oui et PublisherKey.hpp sans cle : la construction s'arrete");
    else
        check(r.ok(), "obligatoires = oui et une vraie cle compilee : accepte");
    r = control({"--licences-obligatoires", "non"});
    check(r.ok(), "obligatoires = non : accepte (les licences pas encore actives)");
    r = control();
    check(r.ok(), "les ecarts retires : accepte de nouveau (" + r.err + ")");
}

} // namespace

int main(int argc, char** argv) {
    g_verbose = std::getenv("XPG_VERBOSE") != nullptr;
    if (argc < 3) {
        std::printf("usage : licensing_test <XPG-License-Admin> <racine du depot> [<libxpg_licensing.a>]\n");
        return 2;
    }
    const std::string admin = argv[1];
    g_repo = argv[2];
    if (!deploy::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::uint8_t rnd[6];
    deploy::crypto::randomBytes(rnd, sizeof rnd);
    g_root = fs::temp_directory_path() / ("xpg-licensing-test-" + deploy::crypto::hex(rnd, sizeof rnd));
    fs::create_directories(g_root);
    g_now = T("2026-11-06T09:00:00Z");
    std::string why;
    g_key = adm::createPublisherKey(U(g_root / "admin"), "", /*test=*/true, &why);
    if (!g_key) {
        std::printf("la cle d'editeur d'essai ne se cree pas : %s\n", why.c_str());
        return 1;
    }
    testFormats();
    testPublisherKey();
    testRegistry();
    testOnePc();
    testRefusals();
    testEditions();
    testNotConfigured();
    testScreen();
    testStation();        // 1.32.0
    testIssues();         // 1.32.0
    testStateRebuild();   // 1.32.0
    testCsvInjection();   // 1.32.0
    testAdminTool(admin);
    testActivations(admin);
    testPackageControl(admin);
    testTestKeyMarker(argc > 3 ? argv[3] : std::string());
    lic::resetForTests();
    g_key.reset();
    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("licensing : %d controles, %d echec(s)%s\n", g_checks, g_failures,
                g_skipped ? (", " + std::to_string(g_skipped) + " partie(s) sautee(s)").c_str() : "");
    return g_failures == 0 ? 0 : 1;
}
