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
//  licensing_test <XPG-License-Admin> <racine du depot>   (XPG_VERBOSE=1 : chaque controle)
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
    check(has(header, "CLE D'ESSAI") && has(header, adm::kTestKeyMarker) && has(header, ", true,") && has(header, g_key->keyId()),
          "PublisherKey.hpp genere : cle d'ESSAI dite, test = true, identifiant");
    bool ascii = std::all_of(header.begin(), header.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    check(ascii, "PublisherKey.hpp genere : ASCII seulement");
    // La cle compilee dans ce depot est une cle d'essai, et le dit.
    bool compiledTest = true;
    for (const auto& k : lic::publisher::kKeys)
        if (!k.test || !has(k.name, adm::kTestKeyMarker)) compiledTest = false;
    check(compiledTest, "src/licensing/PublisherKey.hpp : une cle d'ESSAI, marquee (a remplacer par la vraie avant de livrer)");
    const auto compiled = lic::compiledPublisherKeys();
    check(!compiled.empty() && compiled[0].id == lic::keyIdOf(compiled[0].key), "cle compilee : son identifiant est celui de sa cle");
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
    g_now = T("2027-11-21T00:00:01Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::Expired && s.edition == "FREE" && !s.hasFeature("ai_generation") && s.hasFeature("api_editor")
              && s.licensedEdition == "PROFESSIONAL",
          "au-dela de la grace : Expiree, edition FREE, ai_generation ferme, api_editor ouvert");
    check(has(s.message, "Enregistrer et Exporter restent permis") && has(s.message, "FREE"), "Expiree : jamais de perte de travail, dit (" + s.message + ")");
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
    // L'etat protege efface : les dates des documents signes gardent un ancrage.
    fs::remove(pc / lic::LicenseStore::kStateFile);
    g_now = T("2026-10-01T00:00:00Z");
    s = lic::refresh();
    check(s.status == LicenseStatus::ClockRolledBack, "etat efface + horloge avant l'emission de la licence : Horloge reculee (ancrage des documents)");
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
    const std::string header = readFile(work / "PublisherKey.hpp");
    check(r.ok() && has(header, adm::kTestKeyMarker) && has(header, "namespace licensing::publisher"), "exporter-cle : le C++ de la cle publique, cle d'ESSAI");
    std::string why;
    auto key = adm::loadPublisherKey(dir, &why);
    check(key != nullptr, "la cle creee par l'outil se relit ici (" + why + ")");
    if (!key) return;
    check(has(header, key->keyId()), "le C++ porte l'identifiant de la cle");

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
    const auto control = [&]() { return runProgram(python, {script, "--controler", U(st)}, 120); };
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
    r = control();
    check(r.ok(), "les ecarts retires : accepte de nouveau");
}

} // namespace

int main(int argc, char** argv) {
    g_verbose = std::getenv("XPG_VERBOSE") != nullptr;
    if (argc < 3) {
        std::printf("usage : licensing_test <XPG-License-Admin> <racine du depot>\n");
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
    testAdminTool(admin);
    testPackageControl(admin);
    lic::resetForTests();
    g_key.reset();
    std::error_code ec;
    fs::remove_all(g_root, ec);
    std::printf("licensing : %d controles, %d echec(s)%s\n", g_checks, g_failures,
                g_skipped ? (", " + std::to_string(g_skipped) + " partie(s) sautee(s)").c_str() : "");
    return g_failures == 0 ? 0 : 1;
}
