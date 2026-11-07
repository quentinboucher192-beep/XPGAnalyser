// =============================================================================
//  tests/licensing_service_test.cpp - 1.35.0 : les formats partages du service
//  des licences (licensing/Service), sans reseau
// -----------------------------------------------------------------------------
//  L'adresse, l'appel signe (et ses refus), le code d'activation (forme,
//  lecture souple, empreinte), l'empreinte de la machine, chaque document (aller
//  et retour, champ inconnu, version future) et la chaine de confiance : un
//  certificat signe par l'editeur, un bloc signe par la cle qu'il nomme, un
//  droit absent, une autre cle, un certificat expire.
// =============================================================================
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <random>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;

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

std::unique_ptr<lic::SigningKey> makeKey(const fs::path& dir, const std::string& name) {
    fs::create_directories(dir);
    std::string why;
    auto k = lic::SigningKey::create((dir / "cle.cle").string(), lic::deviceKeySpec(), name, false, &why);
    if (!k) std::printf("  cle impossible : %s\n", why.c_str());
    return k;
}

lic::SignedBlock sign(lic::DocKind kind, const std::string& doc, const lic::SigningKey& k) {
    return lic::signBlock(kind, doc, k.publicKey(), [&](std::string_view m) { return k.sign(m); });
}

} // namespace

int main() {
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-service-" + std::to_string(rng() % 1000000000ULL));
    auto editor = makeKey(root / "editeur", "Editeur essai");
    auto service = makeKey(root / "service", "Service essai");
    auto pc = makeKey(root / "pc", "PC essai");
    auto other = makeKey(root / "autre", "Autre");
    if (!editor || !service || !pc || !other) return 1;
    const lic::Seconds now = 1793961600;   // 2026-11-06T12:00:00Z

    std::printf("-- l'adresse\n");
    check(sv::validUrl("https://xpg-licences.editeur.workers.dev") && sv::validUrl("http://127.0.0.1:8787") && sv::validUrl("http://localhost:1"),
          "les adresses permises");
    check(!sv::validUrl("http://exemple.com") && !sv::validUrl("https://h/chemin") && !sv::validUrl("ftp://h") && !sv::validUrl("https://u@h") &&
              !sv::validUrl("https://") && !sv::validUrl("https://H.com"),
          "les adresses refusees (http ailleurs, chemin, identifiant, majuscules)");

    std::printf("-- l'appel signe\n");
    {
        const std::string body = "{\"code\":\"x\"}";
        const auto h = sv::signRequest("POST", "/v1/activer", body, pc->publicKey(), [&](std::string_view m) { return pc->sign(m); }, now);
        check(h.size() == 4 && h[0].first == sv::kHeaderKey && h[0].second == pc->publicKeyHex(), "quatre en-tetes, la cle du PC");
        const std::string date = h[1].second, nonce = h[2].second, sig = h[3].second;
        std::string why;
        check(sv::verifyRequest("POST", "/v1/activer", body, h[0].second, date, nonce, sig, now + 30, &why), "verifie : " + why);
        check(!sv::verifyRequest("POST", "/v1/activer", body + " ", h[0].second, date, nonce, sig, now, &why), "un corps change : refuse");
        check(!sv::verifyRequest("POST", "/v1/liberer", body, h[0].second, date, nonce, sig, now, &why), "un autre chemin : refuse");
        check(!sv::verifyRequest("GET", "/v1/activer", body, h[0].second, date, nonce, sig, now, &why), "une autre methode : refuse");
        check(!sv::verifyRequest("POST", "/v1/activer", body, other->publicKeyHex(), date, nonce, sig, now, &why), "une autre cle : refuse");
        check(!sv::verifyRequest("POST", "/v1/activer", body, h[0].second, date, nonce, sig, now + 700, &why) && has(why, "heure"),
              "une heure trop loin : refuse");
        const auto h2 = sv::signRequest("POST", "/v1/activer", body, pc->publicKey(), [&](std::string_view m) { return pc->sign(m); }, now);
        check(h2[2].second != nonce, "un nonce neuf a chaque appel");
        check(sv::requestMessage("GET", "/v1/poste", "1", "n", "") ==
                  "XPG-SERVICE-1\nGET\n/v1/poste\n1\nn\ne3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "le message signe, mot pour mot (le corps vide)");
    }

    std::printf("-- le code d'activation\n");
    {
        std::set<std::string> seen;
        for (int i = 0; i < 50; ++i) {
            const std::string c = sv::newActivationCode();
            std::string n;
            check(c.size() == 23 && c.rfind("XPG-", 0) == 0 && sv::normalizeCode(c, n) && sv::formatCode(n) == c, "forme : " + c);
            seen.insert(c);
        }
        check(seen.size() == 50, "50 codes differents");
        std::string n;
        check(sv::normalizeCode("xpg 7k3m-9qpt 2wxd-h8nb", n) && n == "7K3M9QPT2WXDH8NB", "lu avec souplesse");
        check(sv::normalizeCode("OIL0-0000-0000-0000", n) && n == "0110000000000000", "O pour 0, I et L pour 1");
        check(!sv::normalizeCode("XPG-7K3M-9QPT-2WXD", n) && !sv::normalizeCode("XPG-7K3M-9QPT-2WXD-H8NU", n), "trop court, U : refuses");
        check(sv::codeHash("7K3M9QPT2WXDH8NB").size() == 64 && sv::codeHash("7K3M9QPT2WXDH8NB") != sv::codeHash("7K3M9QPT2WXDH8NC"),
              "l'empreinte du code");
    }

    std::printf("-- l'empreinte de la machine\n");
    check(sv::machineHash("{1234}").size() == 32 && sv::machineHash("{1234}") == sv::machineHash("{1234}") &&
              sv::machineHash("{1234}") != sv::machineHash("{1235}") && sv::machineHash("").empty() && !has(sv::machineHash("{1234}"), "1234"),
          "stable, une par machine, ne dit pas l'identifiant");

    std::printf("-- les documents et la chaine de confiance\n");
    {
        sv::ServiceCert c;
        c.key = service->publicKeyHex();
        c.url = "https://xpg-licences.editeur.workers.dev";
        c.issuedAt = now;
        c.expiresAt = now + 365 * lic::kDay;
        c.rights = {sv::kRightActivation, sv::kRightFloating, sv::kRightTime, sv::kRightTrial};
        const std::string certText = lic::writeBlock(sign(lic::DocKind::ServiceCert, sv::writeCert(c), *editor));
        sv::ServiceCert back;
        std::string why;
        check(sv::verifyCert(certText, {editor->publicKey()}, now, back, &why) && back.key == c.key && back.grants("flottante"), "le certificat : " + why);
        check(!sv::verifyCert(certText, {other->publicKey()}, now, back, &why), "pas signe par une cle crue : refuse");
        check(!sv::verifyCert(certText, {editor->publicKey()}, now + 400 * lic::kDay, back, &why) && has(why, "expir"), "expire : refuse");
        const std::string forged = lic::writeBlock(sign(lic::DocKind::ServiceCert, sv::writeCert(c), *service));
        check(!sv::verifyCert(forged, {editor->publicKey()}, now, back, &why), "un certificat que le service se signe lui-meme : refuse");
        (void)sv::verifyCert(certText, {editor->publicKey()}, now, back, &why);

        sv::Activation a;
        a.licenseId = "XPG-2026-00042";
        a.licenseIssuedAt = now - 100;
        a.deviceKey = pc->publicKeyHex();
        a.station = std::string(32, 'a');
        a.number = 3;
        a.issuedAt = now;
        a.until = now + 30 * lic::kDay;
        const auto ab = sign(lic::DocKind::Activation, sv::writeActivation(a), *service);
        sv::Activation ar;
        check(sv::verifyServiceBlock(ab, lic::DocKind::Activation, back, sv::kRightActivation, &why) && sv::parseActivation(ab.document, ar, &why) &&
                  ar.licenseId == a.licenseId && ar.until == a.until && !ar.floating && ar.station == a.station,
              "l'activation : " + why);
        const auto abOther = sign(lic::DocKind::Activation, sv::writeActivation(a), *other);
        check(!sv::verifyServiceBlock(abOther, lic::DocKind::Activation, back, sv::kRightActivation, &why), "signee par une autre cle : refusee");
        sv::ServiceCert narrow = back;
        narrow.rights = {sv::kRightTime};
        check(!sv::verifyServiceBlock(ab, lic::DocKind::Activation, narrow, sv::kRightActivation, &why) && has(why, "droit"), "sans le droit : refusee");
        check(!sv::verifyServiceBlock(ab, lic::DocKind::Lease, back, sv::kRightFloating, &why), "du mauvais genre : refusee");

        sv::Lease l{1, "XPG-2026-00042", pc->publicKeyHex(), now, now + 900};
        sv::Lease lr;
        check(sv::parseLease(sv::writeLease(l), lr, &why) && lr.until == now + 900, "le jeton : " + why);
        sv::SignedTime t{1, now, std::string(32, '0')};
        sv::SignedTime tr;
        check(sv::parseTime(sv::writeTime(t), tr, &why) && tr.time == now, "l'heure : " + why);
        sv::Trial tri{1, pc->publicKeyHex(), sv::machineHash("{m}"), now - 5 * lic::kDay, 30};
        sv::Trial trr;
        check(sv::parseTrial(sv::writeTrial(tri), trr, &why) && trr.start == tri.start && trr.days == 30, "la Decouverte : " + why);
        std::string extra = sv::writeLease(l);
        extra.insert(1, "\"inconnu\":1,");
        check(!sv::parseLease(extra, lr, &why) && has(why, "inconnu"), "un champ inconnu : refuse");
        std::string v2 = sv::writeTime(t);
        v2.replace(v2.find("\"version\":1"), 11, "\"version\":2");
        check(!sv::parseTime(v2, tr, &why) && has(why, "version 1"), "une version future : refusee");
        std::string ms = sv::writeTime(t);
        ms.replace(ms.find("Z\""), 2, ".000Z\"");
        check(!sv::parseTime(ms, tr, &why), "une date avec millisecondes : refusee (le format est AAAA-MM-JJTHH:MM:SSZ)");
        // un fichier de plusieurs blocs
        const std::string file = lic::writeBlock(ab) + certText;
        lic::SignedBlock found;
        check(sv::findBlock(file, lic::DocKind::ServiceCert, found, &why) && found.kind == lic::DocKind::ServiceCert, "trouver un bloc d'un genre");
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
