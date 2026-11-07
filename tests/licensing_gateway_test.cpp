// =============================================================================
//  tests/licensing_gateway_test.cpp - 1.34.0 : les formats de la passerelle
//  GitHub des licences (licensing/Gateway), sans reseau
// -----------------------------------------------------------------------------
//  Le sceau (pour la cle d'un PC, pour celle de l'editeur ; pas pour une autre,
//  pas modifie), le masque du jeton, la configuration, l'etat d'un PC et son
//  contenu, la demande en ligne (le corps d'une issue), l'annulation signee ; les
//  champs inconnus, les versions futures, les identifiants mal ecrits refuses ;
//  le faux GitHub (tests/fake_passerelle.hpp) : raw, Contents, issues, jetons.
// =============================================================================
#include "fake_passerelle.hpp"

#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace gw = licensing::gateway;

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
    const fs::path root = fs::temp_directory_path() / ("xpg-passerelle-" + std::to_string(rng() % 1000000000ULL));
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

    std::printf("-- l'identifiant du poste, l'adresse\n");
    {
        const std::string id = gw::deviceId(pc->publicKey());
        check(gw::isDeviceId(id), "32 hexadecimaux : " + id);
        check(id == gw::deviceId(pc->publicKey()), "stable");
        check(id != gw::deviceId(other->publicKey()), "un par PC");
        check(!has(id, pc->publicKeyHex().substr(0, 16)), "ne revele pas la cle");
        check(gw::validRepo("editeur/passerelle-xpg") && !gw::validRepo("editeur") && !gw::validRepo("a/b/c") && !gw::validRepo("/x") &&
                  !gw::validRepo("a/..") && !gw::validRepo("a b/c"),
              "les noms de depot");
        gw::Address a;
        a.repo = "editeur/passerelle";
        check(gw::rawUrl(a, gw::statusPath(id)) == "https://raw.githubusercontent.com/editeur/passerelle/main/postes/" + id + ".xpgpos", "l'adresse raw");
        check(gw::apiUrl(a, "/issues") == "https://api.github.com/repos/editeur/passerelle/issues", "l'adresse de l'API");
    }

    std::printf("-- la configuration et le masque du jeton\n");
    {
        const std::string token = "essai_jeton_DEMANDES_ESSAI_000000000000_pas_un_vrai";
        const std::string masked = gw::maskToken("editeur/passerelle", token);
        check(!has(masked, "essai_jeton") && !has(masked, "ESSAI"), "le jeton masque n'est pas en clair");
        std::string back;
        check(gw::unmaskToken("editeur/passerelle", masked, back) && back == token, "le masque se retire");
        check(!gw::unmaskToken("autre/depot", masked, back) || back != token, "avec un autre depot, pas le meme jeton");
        gw::Config c;
        c.number = 3;
        c.issuedAt = 1793961600;
        c.repo = "editeur/passerelle";
        c.requestToken = token;
        const std::string text = gw::writeConfig(c);
        check(!has(text, "essai_jeton"), "la configuration ecrite ne montre pas le jeton");
        gw::Config r;
        std::string why;
        check(gw::parseConfig(text, r, &why) && r.number == 3 && r.repo == c.repo && r.requestToken == token && r.issuedAt == c.issuedAt,
              "la configuration se relit : " + why);
        check(!gw::parseConfig("{\"version\":2,\"numero\":1}", r, &why) && has(why, "version 1"), "une version future est refusee");
        std::string extra = text;
        extra.insert(extra.find('{') + 1, "\"inconnu\":1,");
        check(!gw::parseConfig(extra, r, &why) && has(why, "inconnu"), "un champ inconnu est refuse");
    }

    std::printf("-- l'etat d'un PC\n");
    {
        gw::StatusContent content;
        content.message = "Bienvenue";
        content.license = "XPG-LICENSING 1 licence 2\n{}SIGNATURE ...\n";
        gw::DeviceStatus s;
        s.device = gw::deviceId(pc->publicKey());
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
                  rc.license == content.license,
              "le PC lit son contenu");
        for (gw::State st : {gw::State::Received, gw::State::Accepted, gw::State::Refused, gw::State::Cancelled, gw::State::Updated,
                             gw::State::Revoked, gw::State::Removed}) {
            gw::State back{};
            check(gw::parseStateKey(gw::stateKey(st), back) && back == st && !gw::stateLabel(st).empty(), std::string("l'etat ") + gw::stateKey(st));
        }
        std::string bad = text;
        bad.replace(bad.find("acceptee"), 8, "inventee");
        check(!gw::parseDeviceStatus(bad, r, &why), "un etat inconnu est refuse");
        // signe par l'editeur : un bloc << poste >>
        const auto block = lic::signBlock(lic::DocKind::DeviceStatus, text, editor->publicKey(), [&](std::string_view m) { return editor->sign(m); });
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(lic::writeBlock(block), blocks, &why) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::DeviceStatus &&
                  lic::verifyBlock(blocks[0], editor->publicKey()) && !lic::verifyBlock(blocks[0], pc->publicKey()),
              "le bloc << poste >> signe se relit et se verifie");
    }

    std::printf("-- la demande en ligne et l'annulation\n");
    {
        gw::OnlineRequest q;
        q.request = "XPG-LICENSING 1 demande 10\n{\"x\":1}\nSIGNATURE ...\n";
        q.customer = "Client d'essai Industrie";
        q.contact = "essai@exemple.invalid";
        q.message = "Pour l'atelier";
        const std::string id = gw::deviceId(pc->publicKey());
        const std::string body = gw::issueBody(id, editor->keyId(), gw::sealFor(editor->publicKey(), gw::writeOnlineRequest(q)));
        check(!has(body, "Client d'essai") && !has(body, "essai@") && !has(gw::issueTitle(id), "Client"), "rien en clair dans l'issue");
        gw::IssueBody b;
        std::string why, plain;
        std::string crlf;
        for (char c : body) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        check(gw::parseIssueBody(crlf, b, &why) && b.device == id && b.keyId == editor->keyId(), "le corps se relit (meme en CRLF) : " + why);
        gw::OnlineRequest r;
        check(gw::openWith(*editor, b.sealed, plain, &why) && gw::parseOnlineRequest(plain, r, &why) && r.customer == q.customer &&
                  r.request == q.request && r.contact == q.contact && r.message == q.message,
              "l'editeur l'ouvre : " + why);
        check(!gw::openWith(*other, b.sealed, plain, &why), "personne d'autre ne l'ouvre");
        check(!gw::parseIssueBody("Une issue ordinaire\nbonjour\n", b, &why), "une issue ordinaire n'est pas une demande");

        gw::Cancel c;
        c.device = id;
        c.requestNonce = "n0nce";
        c.at = 1793961600;
        const auto block = lic::signBlock(lic::DocKind::Cancel, gw::writeCancel(c), pc->publicKey(), [&](std::string_view m) { return pc->sign(m); });
        const std::string comment = gw::cancelComment(block);
        lic::SignedBlock back;
        gw::Cancel rc;
        check(gw::parseCancelComment(comment, back, &why) && lic::verifyBlock(back, pc->publicKey()) && gw::parseCancel(back.document, rc, &why) &&
                  rc.requestNonce == "n0nce" && rc.device == id,
              "l'annulation se relit et se verifie : " + why);
        check(!lic::verifyBlock(back, other->publicKey()), "signee par ce PC seulement");
        check(!gw::parseCancelComment("Merci !", back, &why), "un commentaire ordinaire n'est pas une annulation");
    }

    std::printf("-- le faux GitHub\n");
    {
        fakegh::FakePasserelle fake;
        fakegh::install(&fake);
        fake.files["passerelle.xpgpas"] = "config";
        auto http = app::ai::makeHttpClient();
        app::ai::HttpRequest get;
        get.method = "GET";
        get.url = "https://raw.githubusercontent.com/" + fake.repo + "/main/passerelle.xpgpas";
        std::string got;
        app::ai::HttpSink sink;
        sink.onData = [&](std::string_view d) { got.append(d); };
        auto resp = http->perform(get, sink);
        check(resp.ok() && resp.status == 200 && got == "config", "raw sans jeton");
        app::ai::HttpRequest put;
        put.method = "PUT";
        put.url = "https://api.github.com/repos/" + fake.repo + "/contents/postes/x.xpgpos";
        put.body = "{\"message\":\"m\",\"content\":\"" + deploy::crypto::base64(std::string("etat")) + "\",\"branch\":\"main\"}";
        put.headers = {{"Authorization", "Bearer " + fake.requestToken}};
        resp = app::ai::makeHttpClient()->perform(put, {});
        check(resp.status == 403, "le jeton des demandes n'ecrit pas de fichier");
        put.headers = {{"Authorization", "Bearer " + fake.adminToken}};
        resp = app::ai::makeHttpClient()->perform(put, {});
        check(resp.status == 201 && fake.files["postes/x.xpgpos"] == "etat", "le jeton de l'editeur ecrit");
        resp = app::ai::makeHttpClient()->perform(put, {});
        check(resp.status == 409, "remplacer sans le sha : 409");
        app::ai::HttpRequest issue;
        issue.method = "POST";
        issue.url = "https://api.github.com/repos/" + fake.repo + "/issues";
        issue.body = "{\"title\":\"t\",\"body\":\"b\"}";
        resp = app::ai::makeHttpClient()->perform(issue, {});
        check(resp.status == 401, "une issue sans jeton : 401");
        issue.headers = {{"Authorization", "Bearer " + fake.requestToken}};
        resp = app::ai::makeHttpClient()->perform(issue, {});
        check(resp.status == 201 && fake.issues.size() == 1, "le jeton des demandes ouvre une issue");
        fake.offline = true;
        resp = app::ai::makeHttpClient()->perform(get, {});
        check(!resp.ok() && resp.failure == app::ai::HttpResponse::Failure::Connect, "hors ligne : Connect");
        fakegh::install(nullptr);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
