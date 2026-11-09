// =============================================================================
//  tests/licensing_gateway_test.cpp - les formats des demandes en ligne et de
//  l'etat d'un poste (licensing/Gateway), echanges par le service des licences
//  (docs/SERVICE-LICENCES.md) ; sans reseau
// -----------------------------------------------------------------------------
//  Le sceau (pour la cle d'un PC, pour celle de l'editeur ; pas pour une autre,
//  pas modifie), l'etat d'un PC et son contenu (le bloc << poste >> signe par
//  l'editeur), la demande en ligne scellee pour l'editeur ; les champs inconnus,
//  les versions futures, les identifiants mal ecrits, les caracteres de controle
//  refuses ; les genres << passerelle >> et << annulation >> de la 1.34.0 sont
//  inconnus (1.35.0).
// =============================================================================
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace gw = licensing::gateway;
namespace svc = licensing::service;

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

} // namespace

int main() {
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-formats-poste-" + std::to_string(rng() % 1000000000ULL));
    auto pc = makeKey(root / "pc", "PC essai");
    auto other = makeKey(root / "autre", "Autre PC");
    auto editor = makeKey(root / "editeur", "Editeur essai");
    if (!pc || !other || !editor) return 1;

    std::printf("-- le sceau\n");
    {
        const std::string sealed = gw::sealFor(pc->publicKey(), "licence secr\xC3\xA8te de Client d'essai");
        check(!sealed.empty(), "sceller pour la cle du PC");
        check(!has(sealed, "Client"), "le sceau ne montre rien en clair");
        std::string plain, why;
        check(gw::openWith(*pc, sealed, plain, &why) && plain == "licence secr\xC3\xA8te de Client d'essai", "le PC l'ouvre : " + why);
        check(!gw::openWith(*other, sealed, plain, &why), "un autre PC ne l'ouvre pas");
        std::string bad = sealed;
        bad[10] = bad[10] == 'A' ? 'B' : 'A';
        check(!gw::openWith(*pc, bad, plain, &why), "un sceau modifie ne s'ouvre pas");
        check(!gw::openWith(*pc, "pas du base64 !", plain, &why), "pas du base64 : refuse");
        const std::string twice = gw::sealFor(pc->publicKey(), "x");
        check(twice != gw::sealFor(pc->publicKey(), "x"), "deux sceaux du meme texte different (cle ephemere)");
    }

    std::printf("-- l'identifiant du poste (celui du service)\n");
    {
        const std::string id = svc::deviceId(pc->publicKey());
        check(svc::isDeviceId(id), "32 hexadecimaux : " + id);
        check(id == svc::deviceId(pc->publicKey()), "stable");
        check(id != svc::deviceId(other->publicKey()), "un par PC");
        check(!has(id, pc->publicKeyHex().substr(0, 16)), "ne revele pas la cle");
    }

    std::printf("-- l'etat d'un PC\n");
    {
        gw::StatusContent content;
        content.message = "Bienvenue";
        content.license = "XPG-LICENSING 1 licence 2\n{}SIGNATURE ...\n";
        content.revocations = "XPG-LICENSING 1 revocations 2\n{}SIGNATURE ...\n";
        gw::DeviceStatus s;
        s.device = svc::deviceId(pc->publicKey());
        s.number = 7;
        s.issuedAt = 1793961600;
        s.state = gw::State::Accepted;
        s.requestNonce = "n0nce";
        s.licenseId = "XPG-2026-00042";
        s.sealed = gw::sealFor(pc->publicKey(), gw::writeStatusContent(content));
        const std::string text = gw::writeDeviceStatus(s);
        check(!has(text, "Bienvenue"), "le message est scelle");
        gw::DeviceStatus r;
        std::string why;
        check(gw::parseDeviceStatus(text, r, &why) && r.state == gw::State::Accepted && r.number == 7 && r.licenseId == s.licenseId &&
                  r.requestNonce == "n0nce",
              "l'etat se relit : " + why);
        std::string plain;
        gw::StatusContent rc;
        check(gw::openWith(*pc, r.sealed, plain, &why) && gw::parseStatusContent(plain, rc, &why) && rc.message == "Bienvenue" &&
                  rc.license == content.license && rc.revocations == content.revocations,
              "le PC lit son contenu");
        for (gw::State st : {gw::State::Received, gw::State::Accepted, gw::State::Refused, gw::State::Cancelled, gw::State::Updated,
                             gw::State::Revoked, gw::State::Removed}) {
            gw::State back{};
            check(gw::parseStateKey(gw::stateKey(st), back) && back == st && !gw::stateLabel(st).empty(), std::string("l'etat ") + gw::stateKey(st));
        }
        std::string bad = text;
        bad.replace(bad.find("acceptee"), 8, "inventee");
        check(!gw::parseDeviceStatus(bad, r, &why), "un etat inconnu est refuse");
        bad = text;
        bad.replace(bad.find(s.device), s.device.size(), "pas-un-poste");
        check(!gw::parseDeviceStatus(bad, r, &why) && has(why, "poste"), "un identifiant de poste mal ecrit est refuse");
        bad = text;
        bad.insert(bad.find('{') + 1, "\"inconnu\":1,");
        check(!gw::parseDeviceStatus(bad, r, &why) && has(why, "inconnu"), "un champ inconnu est refuse");
        check(!gw::parseDeviceStatus("{\"version\":2}", r, &why) && has(why, "version 1"), "une version future est refusee");
        // signe par l'editeur : un bloc << poste >>
        const auto block = lic::signBlock(lic::DocKind::DeviceStatus, text, editor->publicKey(), [&](std::string_view m) { return editor->sign(m); });
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(lic::writeBlock(block), blocks, &why) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::DeviceStatus &&
                  lic::verifyBlock(blocks[0], editor->publicKey()) && !lic::verifyBlock(blocks[0], pc->publicKey()),
              "le bloc << poste >> signe se relit et se verifie");
    }

    std::printf("-- la demande en ligne\n");
    {
        gw::OnlineRequest q;
        q.request = "XPG-LICENSING 1 demande 10\n{\"x\":1}\nSIGNATURE ...\n";
        q.customer = "Client d'essai Industrie";
        q.contact = "essai@exemple.invalid";
        q.message = "Pour l'atelier";
        // ce que le PC envoie au service (POST /v1/demandes : "scelle") ; le service ne l'ouvre pas
        const std::string sealed = gw::sealFor(editor->publicKey(), gw::writeOnlineRequest(q));
        check(!sealed.empty() && !has(sealed, "Client d'essai") && !has(sealed, "essai@"), "rien en clair dans la demande scellee");
        std::string why, plain;
        gw::OnlineRequest r;
        check(gw::openWith(*editor, sealed, plain, &why) && gw::parseOnlineRequest(plain, r, &why) && r.customer == q.customer &&
                  r.request == q.request && r.contact == q.contact && r.message == q.message,
              "l'editeur l'ouvre : " + why);
        check(!gw::openWith(*other, sealed, plain, &why), "personne d'autre ne l'ouvre");
        check(!gw::openWith(*pc, sealed, plain, &why), "pas meme le PC qui l'a scellee");
        // 1.42.0 : une demande d'OPTION (facultative, ecrite seulement si elle sert)
        check(!has(gw::writeOnlineRequest(q), "option"), "sans option : rien d'ecrit (une Admin 1.41 la lit encore)");
        {
            gw::OnlineRequest x = q;
            x.option = "exports_avances";
            x.optionDays = 30;
            x.optionFrom = 1800000000 - 1800000000 % 86400;
            check(gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why) && r.option == "exports_avances" && r.optionDays == 30 && r.optionFrom == x.optionFrom &&
                      r.optionLastDay == 0,
                  "une option, 1 mois, a partir d'un jour : relue telle quelle (" + why + ")");
            x.optionDays = -1;
            x.optionLastDay = x.optionFrom + 9 * 86400;
            check(gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why) && r.optionDays == -1 && r.optionLastDay == x.optionLastDay, "des dates (du ... au ...)");
            x.optionLastDay = x.optionFrom - 86400;
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why) && has(why, "dates"), "des dates a l'envers : refusees");
            x.optionLastDay = 0;
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why), "des dates sans fin : refusees");
            x = q;
            x.option = "Exports Avances";
            x.optionDays = 7;
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why) && has(why, "identifiant"), "un identifiant d'option mal ecrit : refuse");
            std::string orphan = gw::writeOnlineRequest(q);
            orphan.insert(orphan.find('{') + 1, "\"option_jours\":7,");
            check(!gw::parseOnlineRequest(orphan, r, &why) && has(why, "sans option"), "une duree d'option sans option : refusee");
        }
        std::string extra = gw::writeOnlineRequest(q);
        extra.insert(extra.find('{') + 1, "\"inconnu\":1,");
        check(!gw::parseOnlineRequest(extra, r, &why) && has(why, "inconnu"), "un champ inconnu est refuse");
        // 1.34.0 (relecture) : une ligne chacun, sans caractere de controle (la console de l'editeur les affiche)
        for (const std::string& bad : {std::string("Client\nlicence demand\xC3\xA9" "e XPG-1"), std::string("Client \x1b[2J"),
                                      std::string("Client \xC2\x9B" "31m"), std::string("Client \xFF")}) {
            gw::OnlineRequest x = q;
            x.customer = bad;
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why), "un client avec un caractere de controle est refuse");
        }
        {
            gw::OnlineRequest x = q;
            x.message = "ligne 1\rligne 2";
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why), "un message sur deux lignes est refuse");
            x = q;
            x.customer = "   ";
            check(!gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why), "un client vide est refuse");
            x = q;
            x.customer = "Soci\xC3\xA9t\xC3\xA9 d'essai \xE2\x80\x94 atelier \xF0\x9F\x94\xA7";
            check(gw::parseOnlineRequest(gw::writeOnlineRequest(x), r, &why), "des accents, un tiret long, un symbole : acceptes");
        }
    }

    std::printf("-- les genres << passerelle >> et << annulation >> de la 1.34.0 sont inconnus\n");
    {
        // un bloc bien forme (<< demande >>, signe par le PC), puis le meme sous un genre disparu
        const auto block = lic::signBlock(lic::DocKind::Request, "{\"version\":1}", pc->publicKey(), [&](std::string_view m) { return pc->sign(m); });
        const std::string good = lic::writeBlock(block);
        std::vector<lic::SignedBlock> blocks;
        std::string why;
        check(lic::readBlocks(good, blocks, &why) && blocks.size() == 1, "le bloc << demande >> se lit : " + why);
        for (const char* kind : {"passerelle", "annulation"}) {
            std::string text = good;
            text.replace(text.find(" demande "), 9, std::string(" ") + kind + " ");
            blocks.clear();
            why.clear();
            check(!lic::readBlocks(text, blocks, &why) && !why.empty(), std::string("un bloc << ") + kind + " >> est refuse : " + why);
        }
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
