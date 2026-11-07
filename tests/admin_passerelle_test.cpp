// =============================================================================
//  tests/admin_passerelle_test.cpp - 1.34.0 : LA PASSERELLE GITHUB DES LICENCES,
//  COTE EDITEUR (license_admin/Passerelle), sans reseau ni ecran
// -----------------------------------------------------------------------------
//  Le faux GitHub (tests/fake_passerelle.hpp), un poste Admin d'essai
//  (tests/admin_test_fixture.hpp : une cle d'ESSAI, le registre d'exemple) et
//  des PC d'essai (des LicenseStore : leur cle, leur demande en texte, leurs
//  annulations signees) - comme le ferait un Studio :
//    la configuration et le jeton des demandes ; Verifier (un depot prive, un
//    jeton sans droit d'ecrire : refuses) ; publier la configuration (signee, le
//    jeton masque, le numero qui monte) ; relever une demande (le .xpgreq et son
//    fichier a cote, << recue >> publiee une fois) ; accepter (la licence scellee
//    pour CE PC, l'issue fermee, un commentaire sans rien du client) ; une
//    licence partagee (un second PC demande la meme licence : acceptee pour lui,
//    mise a jour pour l'autre) ; refuser (la raison scellee) ; annuler (signe par
//    le PC ; une fausse annulation ignoree) ; une demande scellee pour une autre
//    cle ; deux demandes d'un meme PC ; revoquer ; retirer un PC ; hors ligne,
//    jeton refuse, limite de GitHub ; le kit des Studios (une cle d'essai
//    refusee) ; rien d'un client ni aucun jeton en clair dans le depot.
// =============================================================================
#include "admin_test_fixture.hpp"
#include "fake_passerelle.hpp"

#include "../src/license_admin/GitHubSync.hpp"
#include "../src/license_admin/Passerelle.hpp"
#include "../src/licensing/Gateway.hpp"

#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace admintest;
namespace gw = licensing::gateway;
namespace pas = license_admin::passerelle;
namespace github = license_admin::github;
namespace json = core::json;

namespace {

constexpr lic::Seconds kHour = 3600;

fs::path PP(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// ---- un PC d'essai, comme un Studio ----
struct Pc {
    std::string                         name;
    std::unique_ptr<lic::LicenseStore>  store;
    std::string                         keyHex, device, nonce, request;
    deploy::crypto::PublicKey           pub{};
    int                                 issue{0};
};

int httpPost(fakegh::FakePasserelle& fake, const std::string& tail, const json::Value& body, const std::string& token) {
    app::ai::HttpRequest req;
    req.method = "POST";
    req.url = fake.api + "/repos/" + fake.repo + tail;
    req.body = json::write(body, false);
    req.headers = {{"Authorization", "Bearer " + token}, {"Accept", "application/vnd.github+json"}};
    std::string got;
    app::ai::HttpSink sink;
    sink.onData = [&](std::string_view d) { got.append(d); };
    const auto resp = app::ai::makeHttpClient()->perform(req, sink);
    json::Value v;
    if (resp.status != 201 || !json::parse(got, v) || !v.isObject()) return 0;
    return static_cast<int>(v.integer("number", 1));
}

std::unique_ptr<Pc> makePc(Fixture& fx, const std::string& name) {
    auto pc = std::make_unique<Pc>();
    pc->name = name;
    const fs::path folder = fx.root / ("pc-" + name);
    std::error_code ec;
    fs::create_directories(folder, ec);
    pc->store = std::make_unique<lic::LicenseStore>(fx.pcOptions(folder));
    return pc;
}

// Le Studio : la demande signee, scellee pour l'editeur, dans une issue ouverte avec le jeton des demandes.
bool ask(Pc& pc, fakegh::FakePasserelle& fake, const lic::SigningKey& editor, const std::string& customer, const std::string& licenseId = {},
         const std::string& message = "Pour l'atelier d'essai") {
    std::string why;
    lic::ActivationRequest parsed;
    if (!pc.store->activationRequestText("Poste essai " + pc.name, licenseId, "IHM Studio 1.34.0 (essai)", pc.request, &parsed, &why)) {
        std::printf("  (demande : %s)\n", why.c_str());
        return false;
    }
    pc.nonce = parsed.nonce;
    pc.keyHex = parsed.deviceKey;
    deploy::crypto::Bytes raw;
    if (!deploy::crypto::unhex(pc.keyHex, raw, 32)) return false;
    std::copy(raw.begin(), raw.end(), pc.pub.begin());
    pc.device = gw::deviceId(pc.pub);
    gw::OnlineRequest q;
    q.request = pc.request;
    q.customer = customer;
    q.contact = "essai-" + pc.name + "@exemple.invalid";
    q.message = message;
    const std::string body = gw::issueBody(pc.device, editor.keyId(), gw::sealFor(editor.publicKey(), gw::writeOnlineRequest(q)));
    json::Value v = json::Value::object();
    v.set("title", gw::issueTitle(pc.device));
    v.set("body", body);
    pc.issue = httpPost(fake, "/issues", v, fake.requestToken);
    return pc.issue > 0;
}

// Une annulation signee par `signer` (le PC lui-meme, ou un autre : une fausse) pour la demande de `pc`.
bool cancel(const Pc& pc, const Pc& signer, fakegh::FakePasserelle& fake) {
    gw::Cancel c;
    c.device = pc.device;
    c.requestNonce = pc.nonce;
    c.at = g_now;
    std::string block, why;
    if (!signer.store->deviceSign(lic::DocKind::Cancel, gw::writeCancel(c), block, &why)) {
        std::printf("  (annulation : %s)\n", why.c_str());
        return false;
    }
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(block, blocks, &why) || blocks.size() != 1) return false;
    json::Value v = json::Value::object();
    v.set("body", gw::cancelComment(blocks[0]));
    return httpPost(fake, "/issues/" + std::to_string(pc.issue) + "/comments", v, fake.requestToken) != 0;
}

// L'etat publie d'un poste (signe par l'editeur), et son contenu ouvert par le PC.
bool statusOf(fakegh::FakePasserelle& fake, const lic::SigningKey& editor, const std::string& device, gw::DeviceStatus& out) {
    const auto it = fake.files.find(gw::statusPath(device));
    if (it == fake.files.end()) return false;
    std::vector<lic::SignedBlock> blocks;
    return lic::readBlocks(it->second, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::DeviceStatus &&
           lic::verifyBlock(blocks[0], editor.publicKey()) && gw::parseDeviceStatus(blocks[0].document, out, nullptr) && out.device == device;
}

bool contentOf(const Pc& pc, const gw::DeviceStatus& s, gw::StatusContent& out) {
    std::string plain, why;
    return pc.store->deviceOpenSeal(s.sealed, plain, &why) && gw::parseStatusContent(plain, out, &why);
}

int statusWrites(const fakegh::FakePasserelle& fake, const std::string& device) {
    int n = 0;
    for (const auto& r : fake.requests)
        if (r.rfind("PUT ", 0) == 0 && has(r, "/contents/postes/" + device)) ++n;
    return n;
}

std::optional<pas::Entry> entryOf(const std::vector<pas::Entry>& all, int issue) {
    for (const auto& e : all)
        if (e.issue == issue) return e;
    return std::nullopt;
}

// Un client qui dit que le depot est PRIVE (le reste : le faux GitHub).
class PrivateRepoClient final : public app::ai::HttpClient {
public:
    explicit PrivateRepoClient(std::string url) : url_(std::move(url)) {}
    app::ai::HttpResponse perform(const app::ai::HttpRequest& req, const app::ai::HttpSink& sink) override {
        if (req.method == "GET" && req.url == url_) {
            app::ai::HttpResponse r;
            r.status = 200;
            const std::string body = "{\"full_name\":\"x/y\",\"private\":true,\"permissions\":{\"push\":true,\"pull\":true}}";
            if (sink.onHeaders) sink.onHeaders(200, {});
            if (sink.onData) sink.onData(body);
            return r;
        }
        return inner_.perform(req, sink);
    }
    void cancel() override {}
    [[nodiscard]] bool cancelled() const noexcept override { return false; }

private:
    std::string    url_;
    fakegh::Client inner_;
};

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    Fixture fx(source, "passerelle");
    check(fx.ready(), "le poste d'essai est pret (cle d'essai, registre)");
    const std::string folder = U(fx.admin);
    // Une copie de la cle (celle du Workspace est rechargee a chaque reload).
    std::string keyWhy;
    const auto editorKey = license_admin::loadPublisherKey(folder, &keyWhy);
    if (!editorKey) {
        std::printf("  cle illisible : %s\n", keyWhy.c_str());
        return 1;
    }
    const lic::SigningKey& editor = *editorKey;
    fakegh::FakePasserelle fake;
    fakegh::install(&fake);
    pas::Options opt;
    opt.now = g_now;
    opt.tool = "XPG-License-Admin essai";
    const auto tick = [&] {
        g_now += kHour;
        opt.now = g_now;
        fx.ws->reload();
    };

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la configuration et le jeton des demandes\n");
    {
        pas::Config c;
        std::string why;
        check(!pas::configured(folder) && !pas::readConfig(folder, c, &why) && has(why, "passerelle-config"), "pas reglee : dit comment");
        c.repo = "pas un depot";
        check(!pas::writeConfig(folder, c, &why) && has(why, "PROPRIETAIRE/NOM"), "un depot mal ecrit : refuse");
        c.repo = fake.repo;
        c.api = "https://api.exemple.invalid";
        check(!pas::writeConfig(folder, c, &why) && has(why, "api.github.com"), "une autre API (le jeton y partirait) : refusee");
        c.api = gw::kDefaultApi;
        check(pas::writeConfig(folder, c, &why) && pas::configured(folder), "reglee : " + why);
        pas::Config back;
        check(pas::readConfig(folder, back, &why) && back.repo == fake.repo && back.branch == "main" && back.raw == gw::kDefaultRaw, "relue");
        check(github::saveToken(folder, fake.adminToken, &why), "le jeton GitHub de l'editeur (celui de la synchro)");
        check(!pas::saveRequestToken(folder, fake.adminToken, &why) && has(why, "TON jeton"), "le jeton de l'editeur comme jeton des demandes : refuse");
        check(!pas::saveRequestToken(folder, "court", &why), "un jeton mal forme : refuse");
        check(pas::saveRequestToken(folder, " " + fake.requestToken + "\n", &why) && pas::hasRequestToken(folder), "le jeton des demandes enregistre : " + why);
        std::string token;
        check(pas::loadRequestToken(folder, token, &why) && token == fake.requestToken, "il se relit (espaces retires)");
        check(!has(readFile(fx.admin / pas::kRequestTokenFile), "DEMANDES") && !has(readFile(fx.admin / pas::kConfigFile), "essai_jeton"),
              "aucun jeton en clair dans les fichiers du poste");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- verifier le depot\n");
    {
        app::ai::setHttpFactory([url = fake.api + "/repos/" + fake.repo] { return std::make_unique<PrivateRepoClient>(url); });
        auto r = pas::verify(folder, opt);
        check(!r.ok && has(r.message, "PRIV\xC3\x89") && has(r.message, "SANS jeton"), "un depot prive : refuse, et pourquoi (" + r.message + ")");
        fakegh::install(&fake);
        std::string why;
        check(github::saveToken(folder, fake.requestToken, &why), "(un jeton sans droit d'ecrire comme jeton de l'editeur)");
        r = pas::verify(folder, opt);
        check(!r.ok && has(r.message, "ne peut pas \xC3\xA9" "crire") && has(r.message, "Contents") && has(r.message, "Issues"),
              "sans droit d'ecrire : refuse, ce qui manque est dit (" + r.message + ")");
        check(github::saveToken(folder, fake.adminToken, &why), "(le bon jeton)");
        r = pas::verify(folder, opt);
        check(r.ok && has(r.text(), "public") && has(r.text(), "Jeton des demandes : accept"), "verifie : public, droits, jeton des demandes (" + r.text() + ")");
        check(fake.files.empty() && fake.commits == 0, "verifier n'ecrit rien");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- publier la configuration\n");
    {
        auto r = pas::publishConfig(folder, editor, opt);
        check(r.ok, "publiee : " + r.text());
        gw::Config c;
        std::vector<lic::SignedBlock> blocks;
        std::string why;
        check(lic::readBlocks(fake.files[gw::kConfigPath], blocks, &why) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::Gateway &&
                  lic::verifyBlock(blocks[0], editor.publicKey()) && gw::parseConfig(blocks[0].document, c, &why),
              "passerelle.xpgpas : un bloc << passerelle >> signe par l'editeur (" + why + ")");
        check(c.number == 1 && c.repo == fake.repo && c.requestToken == fake.requestToken, "numero 1, le depot, le jeton des demandes (relu une fois demasque)");
        check(!fake.anyPublicContains(fake.requestToken) && !fake.anyPublicContains("DEMANDES_000"), "le jeton des demandes n'est pas en clair dans le depot");
        check(fake.files.count(gw::kRegistryPath) && lic::readBlocks(fake.files[gw::kRegistryPath], blocks, &why) && blocks[0].kind == lic::DocKind::Registry,
              "le registre de l'archive publie aussi");
        check(!fake.files.count(gw::kRevocationsPath), "pas de liste de revocation : rien a publier");
        tick();
        r = pas::publishConfig(folder, editor, opt);
        check(r.ok && lic::readBlocks(fake.files[gw::kConfigPath], blocks, &why) && gw::parseConfig(blocks[0].document, c, &why) && c.number == 2,
              "republiee : numero 2 (" + r.text() + ")");
        check(has(r.text(), "d\xC3\xA9j\xC3\xA0 \xC3\xA0 jour"), "le registre deja a jour n'est pas reecrit");
        std::string token;
        check(pas::eraseRequestToken(folder, &why) && !pas::hasRequestToken(folder), "le jeton des demandes retire");
        r = pas::publishConfig(folder, editor, opt);
        check(!r.ok && has(r.message, "jeton des demandes"), "sans jeton des demandes : refuse (" + r.message + ")");
        check(pas::saveRequestToken(folder, fake.requestToken, &why), "(remis)");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une demande d'un PC, relevee\n");
    auto pcA = makePc(fx, "A");
    std::string licA;
    const std::string idA = "XPG-2026-00101";
    {
        check(ask(*pcA, fake, editor, "Client d'essai Alpha"), "le PC A ouvre son issue (jeton des demandes)");
        check(!fake.anyPublicContains("Client d'essai") && !fake.anyPublicContains("essai-A@") && !fake.anyPublicContains("atelier d'essai"),
              "rien du client en clair dans l'issue");
        tick();
        auto r = pas::fetchRequests(folder, editor, opt);
        check(r.ok && r.newRequests == 1 && r.pending == 1, "relevee : une nouvelle, en attente (" + r.text() + ")");
        const auto all = pas::listRequests(folder);
        auto e = entryOf(all, pcA->issue);
        check(e && e->state == pas::RequestState::Received && e->customer == "Client d'essai Alpha" && e->deviceName == "Poste essai A" &&
                  e->deviceKey == pcA->keyHex && e->nonce == pcA->nonce && e->software == "IHM Studio 1.34.0 (essai)" && e->contact == "essai-A@exemple.invalid",
              "la demande lue : client, PC, logiciel, contact");
        check(e && fs::exists(PP(e->requestPath)) && readFile(PP(e->requestPath)) == pcA->request, "le .xpgreq range tel quel (l'assistant prend des fichiers)");
        check(e && fs::exists(PP(e->sidecarPath)) && pas::isOnlineRequestPath(folder, e->requestPath), "son fichier a cote (.json), dans demandes-en-ligne");
        check(e && fx.ws->readRequest(e->requestPath).ok, "l'assistant la lit (signee par le PC)");
        gw::DeviceStatus s;
        check(statusOf(fake, editor, pcA->device, s) && s.state == gw::State::Received && s.requestNonce == pcA->nonce && s.number == 1,
              "<< recue >> publiee pour ce poste (signee, n. 1)");
        const int writes = statusWrites(fake, pcA->device);
        tick();
        r = pas::fetchRequests(folder, editor, opt);
        check(r.ok && r.newRequests == 0 && r.pending == 1 && statusWrites(fake, pcA->device) == writes, "relevee de nouveau : << recue >> pas republiee");
        const auto st = pas::readFetchState(folder);
        check(st.fetchedAt == g_now && st.pending == 1 && st.total == 1, "le releve : sa date, ses comptes");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- accepter : la licence publiee pour ce PC\n");
    {
        const auto all = pas::listRequests(folder);
        auto e = entryOf(all, pcA->issue);
        gui::LicenceDraft d;
        d.licenseId = idA;
        d.customer = e ? e->customer : std::string();
        d.edition = "PROFESSIONAL";
        d.requestPaths = {e ? e->requestPath : std::string()};
        const auto out = fx.root / "sortie" / (idA + ".xpglic");
        fs::create_directories(fx.root / "sortie");
        const auto signedR = fx.ws->signLicence(d, U(out));
        check(signedR.ok, "signee : " + signedR.text());
        licA = readFile(out);
        tick();
        const auto r = pas::publishLicence(folder, editor, licA, opt);
        check(r.ok && has(r.message, "1 demande accept"), "publiee : " + r.text());
        gw::DeviceStatus s;
        check(statusOf(fake, editor, pcA->device, s) && s.state == gw::State::Accepted && s.requestNonce == pcA->nonce && s.licenseId == idA && s.number == 2,
              "<< acceptee >>, la demande, la licence, n. 2");
        gw::StatusContent content;
        check(contentOf(*pcA, s, content) && content.license == licA, "le PC A ouvre son contenu : c'est la licence signee");
        const auto imported = pcA->store->importBytes(content.license);
        check(imported.licenseInstalled, "le PC A l'installe (" + imported.why + ")");
        check(!fake.anyPublicContains("Client d'essai") && !fake.anyPublicContains("Poste essai"),
              "rien du client en clair dans le depot (licence scellee)");
        const auto* issue = fake.findIssue(pcA->issue);
        check(issue && issue->state == "closed" && !issue->comments.empty() && has(issue->comments.back().body, "Accept"),
              "l'issue fermee, un commentaire << Acceptee >>");
        check(issue && !has(issue->comments.back().body, "Client") && !has(issue->comments.back().body, idA), "le commentaire ne dit rien du client");
        const auto after = pas::listRequests(folder);
        e = entryOf(after, pcA->issue);
        check(e && e->state == pas::RequestState::Accepted && e->answer == idA && !e->issueOpen, "la demande : acceptee (" + idA + ")");
        tick();
        const auto again = pas::fetchRequests(folder, editor, opt);
        check(again.ok && again.pending == 0, "relevee : plus rien en attente");
        e = entryOf(pas::listRequests(folder), pcA->issue);
        check(e && e->state == pas::RequestState::Accepted, "toujours acceptee");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une licence partagee : un second PC demande la meme licence\n");
    auto pcB = makePc(fx, "B");
    std::string licAB;
    {
        check(ask(*pcB, fake, editor, "Client d'essai Alpha", idA, "Le second poste"), "le PC B demande la licence " + idA);
        tick();
        auto r = pas::fetchRequests(folder, editor, opt);
        auto e = entryOf(pas::listRequests(folder), pcB->issue);
        check(r.ok && e && e->waiting() && e->licenseId == idA, "relevee : B demande " + idA);
        gui::LicenceDraft d;
        d.extend = true;
        d.licenseId = idA;
        d.customer = "Client d'essai Alpha";
        d.edition = "PROFESSIONAL";
        d.requestPaths = {e ? e->requestPath : std::string()};
        const auto out = fx.root / "sortie" / (idA + "-2.xpglic");
        const auto signedR = fx.ws->signLicence(d, U(out));
        check(signedR.ok && fx.ws->licence(idA)->licence.devices.size() == 2, "etendue : deux PC (" + signedR.text() + ")");
        licAB = readFile(out);
        tick();
        r = pas::publishLicence(folder, editor, licAB, opt);
        check(r.ok && has(r.message, "2 PC"), "publiee aux deux PC : " + r.text());
        gw::DeviceStatus sa, sb;
        gw::StatusContent ca, cb;
        check(statusOf(fake, editor, pcA->device, sa) && sa.state == gw::State::Updated && sa.requestNonce.empty() && contentOf(*pcA, sa, ca) && ca.license == licAB,
              "A : << mise_a_jour >>, la nouvelle emission");
        check(statusOf(fake, editor, pcB->device, sb) && sb.state == gw::State::Accepted && sb.requestNonce == pcB->nonce && contentOf(*pcB, sb, cb) && cb.license == licAB,
              "B : << acceptee >>, la meme licence");
        gw::StatusContent wrong;
        check(!contentOf(*pcB, sa, wrong), "B n'ouvre pas le contenu de A (scelle pour A)");
        check(pcB->store->importBytes(cb.license).licenseInstalled && pcA->store->importBytes(ca.license).licenseInstalled, "chacun l'installe");
        const auto* issue = fake.findIssue(pcB->issue);
        check(issue && issue->state == "closed", "l'issue de B fermee");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- refuser (la raison scellee)\n");
    auto pcC = makePc(fx, "C");
    {
        check(ask(*pcC, fake, editor, "Client d'essai Gamma"), "le PC C demande");
        tick();
        (void)pas::fetchRequests(folder, editor, opt);
        auto r = pas::refuse(folder, editor, pcC->device, "", opt);
        check(!r.ok && has(r.message, "raison"), "sans raison : refuse");
        r = pas::refuse(folder, editor, "zz", "x", opt);
        check(!r.ok, "un poste trop court : refuse");
        const std::string reason = "Pas de contrat pour ce poste (essai)";
        r = pas::refuse(folder, editor, pcC->device.substr(0, 10), reason, opt);
        check(r.ok, "refusee : " + r.text());
        gw::DeviceStatus s;
        gw::StatusContent content;
        check(statusOf(fake, editor, pcC->device, s) && s.state == gw::State::Refused && s.requestNonce == pcC->nonce && contentOf(*pcC, s, content) &&
                  content.message == reason && content.license.empty(),
              "<< refusee >> : le PC lit la raison");
        check(!fake.anyPublicContains("Pas de contrat"), "la raison n'est pas en clair dans le depot");
        const auto* issue = fake.findIssue(pcC->issue);
        check(issue && issue->state == "closed" && !issue->comments.empty() && issue->comments.back().body == "Refus\xC3\xA9" "e.", "commentaire << Refusee. >>, issue fermee");
        r = pas::refuse(folder, editor, pcC->device, reason, opt);
        check(!r.ok, "deja refusee : plus rien en attente pour ce poste");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- annuler (signe par le PC) ; une fausse annulation\n");
    auto pcD = makePc(fx, "D");
    auto pcE = makePc(fx, "E");
    {
        check(ask(*pcD, fake, editor, "Client d'essai Delta"), "le PC D demande");
        check(ask(*pcE, fake, editor, "Client d'essai Epsilon"), "le PC E demande (sa cle servira a une fausse annulation)");
        tick();
        (void)pas::fetchRequests(folder, editor, opt);
        check(cancel(*pcD, *pcE, fake), "une annulation de D signee par E");
        tick();
        auto r = pas::fetchRequests(folder, editor, opt);
        auto e = entryOf(pas::listRequests(folder), pcD->issue);
        gw::DeviceStatus s;
        check(e && e->state == pas::RequestState::Received && !e->problems.empty() && has(e->problems.front(), "annulation refus"),
              "la fausse annulation est ignoree, et notee");
        check(statusOf(fake, editor, pcD->device, s) && s.state == gw::State::Received, "D reste << recue >>");
        check(cancel(*pcD, *pcD, fake), "D annule sa demande (signee par D)");
        tick();
        r = pas::fetchRequests(folder, editor, opt);
        e = entryOf(pas::listRequests(folder), pcD->issue);
        check(r.ok && e && e->state == pas::RequestState::Cancelled, "annulee (" + r.text() + ")");
        check(statusOf(fake, editor, pcD->device, s) && s.state == gw::State::Cancelled && s.requestNonce == pcD->nonce, "<< annulee >> publiee");
        const auto* issue = fake.findIssue(pcD->issue);
        check(issue && issue->state == "closed", "l'issue fermee");
        const int writes = statusWrites(fake, pcD->device);
        tick();
        (void)pas::fetchRequests(folder, editor, opt);
        check(statusWrites(fake, pcD->device) == writes, "annulee une fois : pas republiee");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une demande scellee pour une autre cle ; deux demandes d'un meme PC\n");
    {
        std::string why;
        auto other = lic::SigningKey::create(U(fx.root / "autre-editeur.cle"), lic::deviceKeySpec(), "Autre editeur", false, &why);
        auto pcF = makePc(fx, "F");
        check(other && ask(*pcF, fake, *other, "Client d'essai Zeta"), "le PC F scelle sa demande pour une autre cle");
        auto pcG = makePc(fx, "G");
        check(ask(*pcG, fake, editor, "Client d'essai Eta", {}, "premiere"), "le PC G demande une fois");
        tick();
        const int firstG = pcG->issue;
        check(ask(*pcG, fake, editor, "Client d'essai Eta", {}, "seconde"), "puis une seconde fois");
        tick();
        const auto r = pas::fetchRequests(folder, editor, opt);
        const auto all = pas::listRequests(folder);
        auto f = entryOf(all, pcF->issue);
        check(f && f->state == pas::RequestState::Unreadable && !f->problems.empty() && has(f->problems.front(), "scell\xC3\xA9" "e pour une autre cl\xC3\xA9") &&
                  f->customer.empty(),
              "scellee pour une autre cle : listee, illisible, dit");
        auto g1 = entryOf(all, firstG);
        auto g2 = entryOf(all, pcG->issue);
        check(g1 && g2 && g1->state == pas::RequestState::Replaced && g2->waiting() && g2->message == "seconde", "la plus recente l'emporte ; l'autre : remplacee");
        check(r.ok, "le releve reste bon (" + r.text() + ")");
        pas::Entry w;
        check(pas::findWaiting(folder, pcG->device, w, &why) && w.issue == pcG->issue, "la demande en attente de G : la plus recente");
        check(pas::refuse(folder, editor, pcG->device, "Essai : refusee", opt).ok, "G refusee");
        const auto* old = fake.findIssue(firstG);
        check(old && old->state == "closed", "l'issue remplacee de G fermee avec la reponse");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- revoquer\n");
    {
        tick();
        const auto rv = fx.ws->revoke(idA, "", "Fin de contrat (essai)", false);
        check(rv.ok, "revoquee : " + rv.text());
        std::vector<std::string> keys;
        for (const auto& d : fx.ws->licence(idA)->licence.devices) keys.push_back(d.key);
        tick();
        const auto r = pas::publishRevocation(folder, editor, idA, keys, "Fin de contrat (essai)", opt);
        check(r.ok, "publiee : " + r.text());
        check(fake.files.count(gw::kRevocationsPath) && fake.files[gw::kRevocationsPath] == readFile(fx.admin / "archive" / "revocations" / "revocations.xpgrev"),
              "revocations.xpgrev : celle de l'archive");
        gw::DeviceStatus sa, sb;
        gw::StatusContent ca;
        check(statusOf(fake, editor, pcA->device, sa) && sa.state == gw::State::Revoked && sa.licenseId == idA && contentOf(*pcA, sa, ca) &&
                  has(ca.message, "Fin de contrat"),
              "A : << revoquee >>, la raison scellee");
        check(statusOf(fake, editor, pcB->device, sb) && sb.state == gw::State::Revoked, "B : << revoquee >>");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- retirer un PC\n");
    {
        const std::string reqH = fx.request("pc-h", "Poste essai H"), reqI = fx.request("pc-i", "Poste essai I");
        tick();
        const std::string id = "XPG-2026-00102";
        const auto signedR = fx.licence(id, "Client d'essai Theta", "PROFESSIONAL", {reqH, reqI});
        check(signedR.ok, "une licence pour H et I (par fichier)");
        const std::string text = readFile(fx.root / "sortie" / (id + ".xpglic"));
        tick();
        auto r = pas::publishLicence(folder, editor, text, opt);
        check(r.ok && !has(r.message, "accept"), "publiee : deux << mise_a_jour >> (aucune demande en ligne) : " + r.text());
        const auto devs = fx.ws->licence(id)->licence.devices;
        const std::string removed = devs[1].key, kept = devs[0].key;
        tick();
        const auto out = fx.root / "sortie" / (id + "-sans.xpglic");
        const auto rm = fx.ws->removeDevice(id, removed, U(out));
        check(rm.ok, "PC retire : " + rm.text());
        tick();
        r = pas::publishRemoval(folder, editor, readFile(out), removed, opt);
        check(r.ok, "publie : " + r.text());
        const auto idOf = [](const std::string& hex) {
            deploy::crypto::Bytes raw;
            deploy::crypto::PublicKey k{};
            (void)deploy::crypto::unhex(hex, raw, 32);
            std::copy(raw.begin(), raw.end(), k.begin());
            return gw::deviceId(k);
        };
        gw::DeviceStatus sk, sr;
        check(statusOf(fake, editor, idOf(kept), sk) && sk.state == gw::State::Updated && sk.number == 2, "le PC garde : << mise_a_jour >> (n. 2)");
        check(statusOf(fake, editor, idOf(removed), sr) && sr.state == gw::State::Removed && sr.licenseId == id, "le PC retire : << retiree >>");
        check(!pas::publishLicence(folder, editor, "pas une licence", opt).ok, "pas une licence : refuse");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- hors ligne, jeton refuse, limite de GitHub, depot introuvable\n");
    {
        const int commits = fake.commits;
        fake.offline = true;
        auto r = pas::fetchRequests(folder, editor, opt);
        check(!r.ok && has(r.message, "hors ligne"), "hors ligne : refuse, et dit (" + r.message + ")");
        r = pas::publishLicence(folder, editor, licA, opt);
        check(!r.ok && !has(r.message, "publi\xC3\xA9" "e sur") , "hors ligne : la licence n'est pas dite publiee (" + r.message + ")");
        fake.offline = false;
        std::string why;
        check(github::saveToken(folder, "essai_jeton_FAUX_00000000000000000000", &why), "(un jeton faux)");
        r = pas::verify(folder, opt);
        check(!r.ok && has(r.message, "refuse ton jeton"), "jeton refuse : dit (" + r.message + ")");
        check(github::saveToken(folder, fake.adminToken, &why), "(le bon jeton)");
        fake.rateLimited = true;
        r = pas::fetchRequests(folder, editor, opt);
        check(!r.ok && has(r.message, "limite"), "la limite de GitHub : dite (" + r.message + ")");
        fake.rateLimited = false;
        fake.failCount = 1;
        fake.failStatus = 500;
        r = pas::publishConfig(folder, editor, opt);
        check(!r.ok && has(r.message, "500"), "une panne de GitHub : dite (" + r.message + ")");
        check(fake.commits == commits, "rien n'a ete ecrit pendant ces pannes");
        pas::Config c;
        check(pas::readConfig(folder, c, &why), "(la configuration)");
        pas::Config lost = c;
        lost.repo = "editeur-essai/introuvable";
        check(pas::writeConfig(folder, lost, &why), "(un depot qui n'existe pas)");
        r = pas::verify(folder, opt);
        check(!r.ok && has(r.message, "introuvable"), "un depot introuvable : dit (" + r.message + ")");
        check(pas::writeConfig(folder, c, &why), "(le bon depot)");
        check(pas::fetchRequests(folder, editor, opt).ok, "de nouveau en ligne : releve");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- rien d'un client ni aucun jeton en clair\n");
    {
        for (const char* needle : {"Client d'essai", "exemple.invalid", "atelier", "Poste essai", "second poste", "Pas de contrat"})
            check(!fake.anyPublicContains(needle), std::string("pas en clair dans le depot : ") + needle);
        check(!fake.anyPublicContains(fake.adminToken) && !fake.anyPublicContains(fake.requestToken), "aucun jeton dans le depot");
        bool inUrl = false;
        for (const auto& line : fake.requests)
            if (has(line, "essai_jeton")) inUrl = true;
        check(!inUrl, "aucun jeton dans une adresse");
        check(!fake.anyWriteContains(fake.adminToken), "le jeton de l'editeur n'est jamais dans un corps envoye");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le kit des Studios\n");
    {
        auto r = pas::studioKit(folder, &editor, U(fx.root / "kit-essai"), g_now);
        check(!r.ok && has(r.message, "ESSAI") && !fs::exists(fx.root / "kit-essai" / "PublisherKey.hpp"), "une cle d'essai : refusee, rien d'ecrit (" + r.message + ")");
        // Un poste Admin avec une VRAIE cle (creee pour l'essai, jetee a la fin).
        Fixture real(source, "passerelle-kit");
        check(real.ws->createKey("Editeur kit (essai)", false).ok && !real.ws->key().test, "une cle qui n'est pas d'essai");
        const std::string rfolder = U(real.admin);
        r = pas::studioKit(rfolder, real.ws->signingKey(), U(real.root / "kit"), g_now);
        check(!r.ok && has(r.message, "registre"), "sans registre : refuse (" + r.message + ")");
        check(real.ws->signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json").ok, "le registre signe");
        r = pas::studioKit(rfolder, real.ws->signingKey(), U(real.root / "kit"), g_now);
        check(r.ok && has(r.text(), "ATTENTION") && has(readFile(real.root / "kit" / "GatewayAddress.hpp"), "kCompiledRepo = \"\";"),
              "sans passerelle : GatewayAddress.hpp vide, et c'est dit");
        pas::Config c;
        c.repo = "editeur-essai/passerelle-essai";
        c.branch = "principale";
        std::string why;
        check(pas::writeConfig(rfolder, c, &why), "(la passerelle reglee)");
        r = pas::studioKit(rfolder, real.ws->signingKey(), U(real.root / "kit"), g_now);
        check(r.ok, "le kit : " + r.text());
        std::vector<license_admin::ExportedKey> keys;
        bool test = true;
        check(license_admin::readPublisherKeyHeader(readFile(real.root / "kit" / "PublisherKey.hpp"), keys, test, &why) && !test && keys.size() == 1 &&
                  keys[0].id == real.ws->key().id,
              "PublisherKey.hpp : la cle publique de ce poste, pas d'essai");
        check(readFile(real.root / "kit" / "registre.xpgreg") == readFile(real.admin / "archive" / "registre" / "registre.xpgreg"), "registre.xpgreg : celui de l'archive");
        const std::string addr = readFile(real.root / "kit" / "GatewayAddress.hpp");
        const std::string committed = readFile(fs::path(source) / "src" / "licensing" / "GatewayAddress.hpp");
        check(has(addr, "kCompiledRepo = \"editeur-essai/passerelle-essai\";") && has(addr, "kCompiledBranch = \"principale\";") &&
                  has(addr, "namespace licensing::gateway"),
              "GatewayAddress.hpp : le depot et la branche");
        std::string a2 = addr, c2 = committed;
        const auto strip = [](std::string& s, const char* key) {
            const auto p = s.find(key);
            if (p == std::string::npos) return;
            const auto q = s.find('"', p);
            const auto e = s.find('"', q + 1);
            if (q != std::string::npos && e != std::string::npos) s.erase(q + 1, e - q - 1);
        };
        for (auto* s : {&a2, &c2}) {
            strip(*s, "kCompiledRepo");
            strip(*s, "kCompiledBranch");
        }
        check(committed.empty() || a2 == c2, "GatewayAddress.hpp : la meme forme que le fichier du depot (seules les valeurs changent)");
        const std::string readme = readFile(real.root / "kit" / "LISEZ-MOI.txt");
        check(has(readme, "src/licensing/PublisherKey.hpp") && has(readme, "ressources-licences/registre.xpgreg") && has(readme, "src/licensing/GatewayAddress.hpp") &&
                  has(readme, "\r\n"),
              "LISEZ-MOI.txt : ou mettre chaque fichier (CRLF)");
        bool secret = false;
        for (const auto& f : fs::directory_iterator(real.root / "kit")) {
            const std::string t = readFile(f.path());
            if (has(t, "secret=") || has(t, "essai_jeton") || has(t, "PRIVATE") || has(t, "XPGS1-")) secret = true;
        }
        check(!secret, "rien de secret dans le kit");
        check(fs::directory_iterator(real.root / "kit") != fs::directory_iterator() &&
                  std::distance(fs::directory_iterator(real.root / "kit"), fs::directory_iterator()) == 4,
              "quatre fichiers");
    }

    fakegh::install(nullptr);
    return finish();
}
