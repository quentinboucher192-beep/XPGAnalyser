// =============================================================================
//  tests/licence_gateway_test.cpp - 1.34.0 : LA DEMANDE EN LIGNE DES STUDIOS
//  (app/LicenceGateway : la passerelle GitHub, cote client), sans reseau
// -----------------------------------------------------------------------------
//  Le faux GitHub (tests/fake_passerelle.hpp) ; une cle d'ESSAI de l'editeur
//  creee ici (l'outil Admin, dans ce processus) ; le registre d'exemple signe ;
//  un dossier de licences temporaire. On verifie :
//    - la disponibilite (pas de passerelle ; licences pas activees) ;
//    - la configuration : absente, signee par une cle inconnue, pour un autre
//      depot -> rien n'est envoye ;
//    - envoyer : l'issue creee, rien du client en clair, l'editeur l'ouvre ;
//      une seconde demande refusee tant que la premiere attend ;
//    - annuler : un commentaire signe par CE PC, l'issue fermee, une nouvelle
//      demande permise ;
//    - verifier : acceptee -> la licence s'installe (Valide) ; rien de nouveau ;
//      un etat plus ancien ignore ; un etat d'un autre poste, signe par une cle
//      inconnue, scelle pour un autre PC : refuses ; refusee -> la raison ;
//    - hors ligne, la limite de GitHub, un jeton perime : une phrase, rien ne
//      change ;
//    - le fil de fond : un travail lance, releve (poll), l'avis de la cloche et
//      le message de la barre d'etat ;
//    - le jeton des demandes : jamais dans une adresse, un message, l'historique,
//      le releve local ;
//    - 1.34.0 (relecture de securite) : l'etat ne porte plus l'identifiant de la
//      licence en clair (celui de la licence installee est dit) ; la liste de
//      revocation scellee dans l'etat rend la licence Revoquee ; un etat
//      << recue >>, << annulee >>, << refusee >> pour une autre demande (rejoue)
//      ne change pas la demande en cours ; une licence qui ne s'installe pas
//      (liee a un autre PC) n'est pas marquee traitee et est reprise ; deux
//      Studios sur le meme dossier ne perdent pas la demande envoyee.
// =============================================================================
#include "fake_passerelle.hpp"

#include "../src/app/BackgroundTasks.hpp"
#include "../src/app/Licence.hpp"
#include "../src/app/LicenceGateway.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/Licensing.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace lgw = licensing::gateway;
namespace adm = license_admin;
namespace lg = app::licence::gateway;

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
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void writeFile(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}

lic::TrustedKey trustedOf(const lic::SigningKey& k) {
    lic::TrustedKey t;
    t.id = k.keyId();
    t.name = k.name();
    t.key = k.publicKey();
    t.test = k.test();
    return t;
}

// Tout ce qui a ete dit (les messages des travaux) : le jeton n'y est jamais.
std::vector<std::string> g_said;
lg::Result keep(lg::Result r) {
    g_said.push_back(r.message);
    g_said.push_back(r.notice);
    return r;
}

// ---- l'editeur (dans ce processus) ----
std::string signedConfig(const lic::SigningKey& key, std::uint64_t number, const std::string& repo, const std::string& token) {
    lgw::Config c;
    c.number = number;
    c.issuedAt = lic::systemNow();
    c.repo = repo;
    c.requestToken = token;
    return lic::writeBlock(lic::signBlock(lic::DocKind::Gateway, lgw::writeConfig(c), key.publicKey(),
                                          [&](std::string_view m) { return key.sign(m); }));
}

std::string signedStatus(const lic::SigningKey& key, const lgw::DeviceStatus& s) {
    return lic::writeBlock(lic::signBlock(lic::DocKind::DeviceStatus, lgw::writeDeviceStatus(s), key.publicKey(),
                                          [&](std::string_view m) { return key.sign(m); }));
}

lgw::DeviceStatus status(const std::string& device, std::uint64_t number, lgw::State state, const std::string& nonce, const std::string& licenseId,
                         const lic::crypto::PublicKey& sealFor, const lgw::StatusContent& content) {
    lgw::DeviceStatus s;
    s.device = device;
    s.number = number;
    s.issuedAt = lic::systemNow();
    s.state = state;
    s.requestNonce = nonce;
    s.licenseId = licenseId;
    s.sealed = lgw::sealFor(sealFor, lgw::writeStatusContent(content));
    return s;
}

// L'editeur ouvre une issue : la demande signee par le PC, et ce qu'en dit le client.
bool openIssue(const lic::SigningKey& key, const fakegh::Issue& issue, lgw::OnlineRequest& out, lic::ActivationRequest& req, std::string& device,
               std::string& why) {
    lgw::IssueBody b;
    std::string plain;
    if (!lgw::parseIssueBody(issue.body, b, &why) || b.keyId != key.keyId() || !lgw::openWith(key, b.sealed, plain, &why) ||
        !lgw::parseOnlineRequest(plain, out, &why))
        return false;
    std::vector<lic::SignedBlock> blocks;
    if (!lic::readBlocks(out.request, blocks, &why) || blocks.size() != 1 || blocks[0].kind != lic::DocKind::Request) return false;
    if (!lic::parseRequest(blocks[0].document, req, &why)) return false;
    device = b.device;
    return true;
}

bool pcKey(lic::crypto::PublicKey& pk) {
    std::string hex, why;
    lic::crypto::Bytes b;
    if (!lic::deviceKeyHex(hex, &why) || !lic::crypto::unhex(hex, b, pk.size())) return false;
    std::copy(b.begin(), b.end(), pk.begin());
    return true;
}

int countPasserelle() {
    int n = 0;
    for (const auto& h : lic::getHistory(0)) n += h.kind == "passerelle";
    return n;
}

lg::SendForm form(const std::string& pc, const std::string& licenseId = {}) {
    lg::SendForm f;
    f.customer = "Client d'essai Industrie";
    f.contact = "essai@exemple.invalid";
    f.pcName = pc;
    f.licenseId = licenseId;
    f.message = "Pour l'atelier d'essai";
    return f;
}

std::string licenceFor(const lic::SigningKey& key, const std::string& registry, const std::string& id, const std::string& request) {
    adm::LicenseInputs in;
    in.params = "{\"license_id\": \"" + id + "\", \"customer\": \"Client d'essai\", \"edition\": \"PERSONAL\", \"max_devices\": 2, \"duration_days\": 365}";
    in.requests = {request};
    in.registry = registry;
    std::string out, why, summary;
    if (!adm::signLicense(key, in, lic::systemNow(), out, &why, &summary)) std::printf("  licence impossible : %s\n", why.c_str());
    return out;
}

// 1.34.0 (relecture) : un crochet sur le transport - l'AUTRE Studio (le meme dossier) agit pendant une requete de
// celui-ci. Vrai : fait (une seule fois).
std::function<bool(const app::ai::HttpRequest&)> g_hook;
class HookClient final : public app::ai::HttpClient {
public:
    app::ai::HttpResponse perform(const app::ai::HttpRequest& req, const app::ai::HttpSink& sink) override {
        if (g_hook) {
            auto h = std::move(g_hook);
            g_hook = nullptr;   // pas de reentree (l'autre Studio fait ses propres requetes)
            if (!h(req)) g_hook = std::move(h);
        }
        return inner_.perform(req, sink);
    }
    void cancel() override {}
    [[nodiscard]] bool cancelled() const noexcept override { return false; }

private:
    fakegh::Client inner_;
};

} // namespace

int main(int argc, char** argv) {
    const fs::path repo = argc > 1 ? fs::path(argv[1]) : fs::path(".");
    std::mt19937_64 rng(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path base = fs::temp_directory_path() / ("xpg-licence-passerelle-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(base, ec);

    std::printf("-- la phrase de l'evaluation\n");
    {
        lic::LicenseState st;
        st.status = lic::LicenseStatus::Evaluation;
        st.evaluationDaysLeft = 12;
        (void)lic::parseIsoTime("2026-11-19T10:00:00Z", st.evaluationEndsAt);
        const std::string t = app::licence::evaluationText(st);
        check(t == "\xC3\x89valuation : il te reste 12 jours (jusqu'au 19/11/2026).", "12 jours : " + t);
        st.evaluationDaysLeft = 1;
        check(has(app::licence::evaluationText(st), "il te reste 1 jour (jusqu"), "1 jour, au singulier");
        st.status = lic::LicenseStatus::Valid;
        check(app::licence::evaluationText(st).empty(), "pas en evaluation : rien a dire");
    }

    std::string why;
    auto key = adm::createPublisherKey(U(base / "editeur"), "essai passerelle Studios", true, &why);
    auto stranger = adm::createPublisherKey(U(base / "inconnue"), "essai cle inconnue", true, &why);
    auto otherPc = lic::SigningKey::create(U(base / "autre-pc" / "cle.cle"), lic::deviceKeySpec(), "Poste essai B", false, &why);
    check(key && stranger && otherPc, "les cles d'essai (" + why + ")");
    if (!key || !stranger || !otherPc) {
        std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
        return 1;
    }
    std::string registry;
    check(adm::signRegistry(*key, readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json"), lic::systemNow(), registry, &why),
          "le registre signe (" + why + ")");
    writeFile(base / "livre" / "registre.xpgreg", registry);

    fakegh::FakePasserelle fake;
    fakegh::install(&fake);
    lgw::Address address;
    address.repo = fake.repo;
    address.branch = fake.branch;
    address.api = fake.api;
    address.raw = fake.raw;

    std::printf("-- la disponibilite\n");
    {
        lic::StoreOptions o;
        o.folder = U(base / "sans-cle");
        o.compiledKeys = false;
        app::licence::start(std::move(o));
        lg::setAddressForTests(std::nullopt);
        if (lgw::compiledAddress().empty()) {
            check(!lg::available(&why) && has(why, "pas de passerelle"), "ce programme sans passerelle compilee : " + why);
        }
        lg::setAddressForTests(address);
        check(!lg::available(&why) && has(why, "pas activ"), "licences pas activees : " + why);
        const auto r = keep(lg::check());
        check(!r.ok && fake.requests.empty(), "pas activees : aucune requete");
    }

    std::printf("-- l'evaluation : l'avis a chaque lancement\n");
    {
        lic::StoreOptions o;
        o.folder = U(base / "evaluation");
        o.trustedKeys = {trustedOf(*key)};
        o.compiledKeys = false;
        o.bundledRegistry = U(base / "livre" / "registre.xpgreg");
        o.evaluationPeriod = true;
        app::licence::start(std::move(o));
        app::licence::startNotices(false);   // les avis, sans la passerelle
        const auto st = lic::getLicenseState();
        check(st.status == lic::LicenseStatus::Evaluation && st.evaluationDaysLeft > 0, "un PC neuf : Evaluation (" + st.message + ")");
        const std::string text = app::licence::launchNotice();
        check(has(text, "\xC3\x89valuation : il te reste " + std::to_string(st.evaluationDaysLeft) + " jour") && has(text, "(jusqu'au "),
              "la phrase de l'accueil : " + text);
        bool bell = false;
        for (const auto& n : app::bgtasks::notices()) bell = bell || (n.key == "licence:evaluation" && n.title == text && n.action == "help.licence");
        check(bell, "l'avis de la cloche (bouton Licence...)");
        const auto b = app::licence::badge();
        check(has(b.text, "valuation") && has(b.text, "j restant") && b.tone == 2, "la pastille : " + b.text);
    }

    lic::StoreOptions opts;
    opts.folder = U(base / "pc");
    opts.trustedKeys = {trustedOf(*key)};
    opts.compiledKeys = false;
    opts.bundledRegistry = U(base / "livre" / "registre.xpgreg");
    app::licence::start(opts);
    check(lg::available(&why), "licences actives et une passerelle : disponible (" + why + ")");
    {
        bool bell = false;
        for (const auto& n : app::bgtasks::notices()) bell = bell || n.key == "licence:evaluation";
        check(!bell && app::licence::launchNotice().empty(), "pas en evaluation : l'avis est retire");
    }

    std::printf("-- la configuration de la passerelle\n");
    {
        auto r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "pas encore de configuration") && fake.issues.empty(), "pas de configuration : rien n'est envoye (" + r.message + ")");
        fake.files[lgw::kConfigPath] = signedConfig(*stranger, 1, fake.repo, fake.requestToken);
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "ne conna") && fake.issues.empty(), "signee par une cle inconnue : refusee (" + r.message + ")");
        r = keep(lg::check());
        check(!r.ok && has(r.message, "refus"), "verifier : la meme configuration refusee (" + r.message + ")");
        fake.files[lgw::kConfigPath] = signedConfig(*key, 1, "editeur-essai/autre-depot", fake.requestToken);
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "autre d\xC3\xA9p\xC3\xB4t") && fake.issues.empty(), "pour un autre depot : refusee (" + r.message + ")");
        r = keep(lg::send(lg::SendForm{}));
        check(!r.ok && has(r.message, "soci\xC3\xA9t\xC3\xA9"), "sans nom de societe : refusee avant tout (" + r.message + ")");
        fake.files[lgw::kConfigPath] = signedConfig(*key, 2, fake.repo, fake.requestToken);
    }

    std::printf("-- envoyer la demande\n");
    lg::Record rec;
    {
        const std::size_t before = fake.requests.size();
        const auto r = keep(lg::send(form("Poste essai A")));
        check(r.ok && r.pendingAfter && fake.issues.size() == 1, "la demande part : une issue (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && rec.issue == 1 && lg::pending(rec) && rec.local == lg::kLocalSent && !rec.nonce.empty() &&
                  rec.customer == "Client d'essai Industrie" && rec.pcName == "Poste essai A",
              "le releve local : l'issue n\xC2\xB0 1, en attente (" + why + ")");
        check(fs::exists(base / "pc" / lg::kRecordFile), "demande-en-ligne.json dans le dossier des licences");
        if (!fake.issues.empty()) {
            const auto& issue = fake.issues[0];
            check(issue.title == lgw::issueTitle(rec.device), "le titre : " + issue.title);
            check(!fake.anyPublicContains("Client d'essai") && !fake.anyPublicContains("essai@exemple") && !fake.anyPublicContains("atelier d'essai") &&
                      !fake.anyPublicContains("Poste essai A"),
                  "rien du client en clair dans le depot public (nom, contact, message, nom du PC)");
            lgw::OnlineRequest q;
            lic::ActivationRequest req;
            std::string device;
            check(openIssue(*key, issue, q, req, device, why) && q.customer == "Client d'essai Industrie" && q.contact == "essai@exemple.invalid" &&
                      q.message == "Pour l'atelier d'essai" && req.deviceName == "Poste essai A" && req.nonce == rec.nonce && device == rec.device &&
                      has(req.software, "1.3"),
                  "l'editeur ouvre la demande : le client, le contact, le message, la demande signee par le PC (" + why + ")");
            std::string plain;
            lgw::IssueBody b;
            check(lgw::parseIssueBody(issue.body, b, &why) && !lgw::openWith(*stranger, b.sealed, plain, &why), "une autre cle ne l'ouvre pas");
        }
        bool tokenInHeader = false;
        for (const auto& t : fake.authSeen) tokenInHeader = tokenInHeader || t == fake.requestToken;
        check(tokenInHeader, "le jeton des demandes est passe dans l'en-tete");
        bool rawWithoutToken = true;
        for (std::size_t i = before; i < fake.requests.size(); ++i) rawWithoutToken = rawWithoutToken && !has(fake.requests[i], "essai_jeton_");
        check(rawWithoutToken, "aucune adresse ne porte le jeton");
        const auto again = keep(lg::send(form("Poste essai A")));
        check(!again.ok && has(again.message, "d\xC3\xA9j\xC3\xA0 en cours") && fake.issues.size() == 1, "une seconde demande : refusee (" + again.message + ")");
    }

    std::printf("-- annuler\n");
    {
        const std::string nonce = rec.nonce;
        const auto r = keep(lg::cancel());
        check(r.ok && !r.pendingAfter, "annulee (" + r.message + ")");
        check(!fake.issues.empty() && fake.issues[0].state == "closed" && fake.issues[0].comments.size() == 1, "l'issue fermee, un commentaire");
        if (!fake.issues.empty() && !fake.issues[0].comments.empty()) {
            lic::SignedBlock block;
            lgw::Cancel c;
            lic::crypto::PublicKey pk{};
            check(pcKey(pk) && lgw::parseCancelComment(fake.issues[0].comments[0].body, block, &why) && lic::verifyBlock(block, pk) &&
                      !lic::verifyBlock(block, otherPc->publicKey()) && lgw::parseCancel(block.document, c, &why) && c.requestNonce == nonce &&
                      c.device == rec.device,
                  "le commentaire : l'annulation signee par CE PC, pour cette demande (" + why + ")");
        }
        check(lg::loadRecord(rec, &why) && rec.local == lg::kLocalCancelled && !lg::pending(rec), "le releve : annulee");
        check(!keep(lg::cancel()).ok, "rien a annuler ensuite");
        const auto r2 = keep(lg::send(form("Poste essai A", "")));
        check(r2.ok && fake.issues.size() == 2 && lg::loadRecord(rec, &why) && rec.issue == 2 && rec.nonce != nonce, "une nouvelle demande est permise (" + r2.message + ")");
    }

    lic::crypto::PublicKey pk{};
    check(pcKey(pk), "la cle publique de ce PC");
    const std::string myId = rec.device;

    std::printf("-- acceptee : la licence s'installe d'elle-meme\n");
    {
        lgw::OnlineRequest q;
        lic::ActivationRequest req;
        std::string device;
        check(fake.issues.size() == 2 && openIssue(*key, fake.issues[1], q, req, device, why), "l'editeur ouvre la demande n\xC2\xB0 2");
        const std::string licFile = licenceFor(*key, registry, "XPG-PAS-0001", q.request);
        check(!licFile.empty(), "la licence signee pour ce PC");
        lgw::StatusContent content;
        content.license = licFile;
        content.message = "Bienvenue (essai)";
        // 1.34.0 (relecture) : l'editeur n'ecrit plus l'identifiant de la licence en clair dans l'etat.
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 5, lgw::State::Accepted, rec.nonce, "", pk, content));
        check(!fake.anyPublicContains("XPG-PAS-0001"), "l'identifiant de la licence n'est pas en clair dans le depot");
        check(!fake.anyPublicContains("Bienvenue") && !fake.anyPublicContains("Client d'essai"), "l'etat publie : la licence et le mot scelles");
        const int histBefore = countPasserelle();
        const auto r = keep(lg::check());
        const auto st = lic::getLicenseState();
        check(r.ok && r.licenseInstalled && st.status == lic::LicenseStatus::Valid && st.licenseId == "XPG-PAS-0001",
              "verifier : la licence installee, Valide (" + r.message + " / " + st.message + ")");
        check(has(r.notice, "Ta licence XPG-PAS-0001 est install\xC3\xA9" "e (re\xC3\xA7ue de l'\xC3\xA9" "diteur)."), "l'avis : " + r.notice);
        check(has(r.message, "Bienvenue (essai)"), "le mot de l'editeur est dit");
        check(lg::loadRecord(rec, &why) && rec.outcome == "acceptee" && !lg::pending(rec) && rec.lastNumber == 5 && rec.receivedLicense == "XPG-PAS-0001",
              "le releve : acceptee, etat n\xC2\xB0 5");
        check(countPasserelle() == histBefore + 1, "l'historique : une ligne << passerelle >>");
        const auto again = keep(lg::check());
        check(again.ok && !again.licenseInstalled && has(again.message, "Rien de nouveau") && countPasserelle() == histBefore + 1,
              "verifier encore : rien de nouveau, pas de bruit dans l'historique (" + again.message + ")");
    }

    std::printf("-- le registre et la liste de revocation publies\n");
    {
        std::string text = readFile(repo / "tests" / "fixtures" / "licences" / "registre-exemple.json");
        if (const auto at = text.find("\"number\": 318"); at != std::string::npos) text.replace(at, 13, "\"number\": 319");
        std::string reg319, revocations;
        check(adm::signRegistry(*key, text, lic::systemNow(), reg319, &why), "le registre n\xC2\xB0 319 signe (" + why + ")");
        check(adm::signRevocations(*key, "{\"number\": 2, \"revoked\": []}", lic::systemNow(), revocations, &why), "la liste n\xC2\xB0 2 signee (" + why + ")");
        fake.files[lgw::kRegistryPath] = reg319;
        fake.files[lgw::kRevocationsPath] = revocations;
        const auto imports = [] {
            int n = 0;
            for (const auto& h : lic::getHistory(0)) n += h.kind == "import";
            return n;
        };
        const int before = imports();
        auto r = keep(lg::check());
        auto st = lic::getLicenseState();
        check(r.ok && r.changed && st.registryNumber == 319 && st.revocationNumber == 2 && has(r.message, "n\xC2\xB0 319 est install"),
              "plus recents : adoptes (" + r.message + ")");
        check(st.status == lic::LicenseStatus::Valid, "la licence reste valide");
        const int after = imports();
        r = keep(lg::check());
        check(r.ok && !r.changed && imports() == after && after > before, "deja a jour : pas importes de nouveau (pas de bruit dans l'historique)");
        fake.files[lgw::kRegistryPath] = registry;   // le n\xC2\xB0 318, plus ancien
        r = keep(lg::check());
        check(r.ok && lic::getLicenseState().registryNumber == 319 && imports() == after, "un registre plus ancien publie : ignore");
    }

    std::printf("-- les etats refuses\n");
    {
        lgw::StatusContent empty;
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 3, lgw::State::Revoked, "", "XPG-PAS-0001", pk, empty));
        auto r = keep(lg::check());
        check(r.ok && has(r.message, "plus ancien") && lic::getLicenseState().status == lic::LicenseStatus::Valid, "un etat plus ancien : ignore (" + r.message + ")");
        const std::string otherId = lgw::deviceId(otherPc->publicKey());
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(otherId, 6, lgw::State::Revoked, "", "XPG-PAS-0001", pk, empty));
        r = keep(lg::check());
        check(!r.ok && has(r.message, "autre poste"), "l'etat d'un autre poste : refuse (" + r.message + ")");
        fake.files[lgw::statusPath(myId)] = signedStatus(*stranger, status(myId, 6, lgw::State::Revoked, "", "XPG-PAS-0001", pk, empty));
        r = keep(lg::check());
        check(!r.ok && has(r.message, "ne conna"), "un etat signe par une cle inconnue : refuse (" + r.message + ")");
        std::string tampered = signedStatus(*key, status(myId, 6, lgw::State::Revoked, "", "XPG-PAS-0001", pk, empty));
        tampered.replace(tampered.find("revoquee"), 8, "retiree\"");
        fake.files[lgw::statusPath(myId)] = tampered;
        r = keep(lg::check());
        check(!r.ok, "un etat modifie apres la signature : refuse (" + r.message + ")");
        lgw::StatusContent forOther;
        forOther.license = "XPG-LICENSING 1 licence 2\n{}";
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 6, lgw::State::Accepted, "", "", otherPc->publicKey(), forOther));
        const int histBefore = countPasserelle();
        r = keep(lg::check());
        check(r.ok && !r.licenseInstalled && r.retryLater && has(r.message, "ne s'ouvre pas") && has(r.notice, "Nouvel essai") &&
                  lic::getLicenseState().licenseId == "XPG-PAS-0001",
              "une licence scellee pour un autre PC : rien d'installe, a reprendre (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && rec.lastNumber == 5 && has(rec.lastMessage, "ne s'ouvre pas"),
              "le releve : le n\xC2\xB0 6 n'est PAS marque traite (dernier : n\xC2\xB0 5), la raison gardee");
        r = keep(lg::check());
        check(r.ok && r.retryLater && has(r.message, "ne s'ouvre pas") && r.notice.empty() && countPasserelle() == histBefore + 1,
              "verifier encore : le n\xC2\xB0 6 repris, sans nouvel avis ni nouvelle ligne d'historique (" + r.message + ")");
    }

    std::printf("-- refusee, avec sa raison\n");
    {
        auto r = keep(lg::send(form("Poste essai A", "XPG-PAS-0001")));
        check(r.ok && fake.issues.size() == 3 && lg::loadRecord(rec, &why) && rec.licenseId == "XPG-PAS-0001", "une demande pour ajouter ce PC a une licence (" + r.message + ")");
        lgw::StatusContent content;
        content.message = "La licence XPG-PAS-0001 a deja tous ses PC (essai)";
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 7, lgw::State::Refused, rec.nonce, "", pk, content));
        r = keep(lg::check());
        check(r.ok && r.refused && has(r.message, "a deja tous ses PC (essai)") &&
                  has(r.notice, "Ta demande d'activation a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e : La licence XPG-PAS-0001 a deja tous ses PC (essai)."),
              "refusee : la raison (" + r.notice + ")");
        check(lg::loadRecord(rec, &why) && rec.outcome == "refusee" && !lg::pending(rec) && has(rec.lastMessage, "tous ses PC"), "le releve : refusee, la raison");
        check(lic::getLicenseState().status == lic::LicenseStatus::Valid, "la licence deja installee reste");
    }

    std::printf("-- hors ligne, la limite de GitHub, un jeton perime\n");
    {
        const std::string recordBefore = readFile(base / "pc" / lg::kRecordFile);
        const std::size_t issues = fake.issues.size();
        fake.offline = true;
        auto r = keep(lg::check());
        check(!r.ok && has(r.message, "hors ligne") && has(r.message, "Rien n'a chang"), "verifier hors ligne : " + r.message);
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "hors ligne") && fake.issues.size() == issues, "envoyer hors ligne : " + r.message);
        fake.offline = false;
        check(readFile(base / "pc" / lg::kRecordFile) == recordBefore, "hors ligne : le releve n'a pas change");
        fake.rateLimited = true;
        r = keep(lg::check());
        check(!r.ok && has(r.message, "limite les requ"), "verifier, la limite de GitHub : " + r.message);
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "limite les requ") && fake.issues.size() == issues, "envoyer, la limite de GitHub : " + r.message);
        fake.rateLimited = false;
        fake.failCount = 1;
        fake.failStatus = 502;
        r = keep(lg::check());
        check(!r.ok && has(r.message, "502"), "une panne de GitHub : " + r.message);
        check(readFile(base / "pc" / lg::kRecordFile) == recordBefore, "rien n'a change dans le releve");
        fake.files[lgw::kConfigPath] = signedConfig(*key, 3, fake.repo, "essai_jeton_PERIME_0000000000000000000");
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "jeton des demandes") && fake.issues.size() == issues, "un jeton perime : " + r.message);
        fake.files[lgw::kConfigPath] = signedConfig(*key, 1, fake.repo, fake.requestToken);
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && has(r.message, "plus ancienne") && fake.issues.size() == issues, "une configuration plus ancienne que la derniere vue : " + r.message);
        fake.files[lgw::kConfigPath] = signedConfig(*key, 4, fake.repo, fake.requestToken);
    }

    std::printf("-- le fil de fond\n");
    {
        auto r = keep(lg::send(form("Poste essai A")));
        check(r.ok && fake.issues.size() == 4 && lg::loadRecord(rec, &why), "une demande de plus (" + r.message + ")");
        lgw::OnlineRequest q;
        lic::ActivationRequest req;
        std::string device;
        check(openIssue(*key, fake.issues.back(), q, req, device, why), "l'editeur l'ouvre");
        lgw::StatusContent content;
        content.license = licenceFor(*key, registry, "XPG-PAS-0002", q.request);
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 8, lgw::State::Accepted, rec.nonce, "", pk, content));
        (void)app::licence::takeMessages();
        const auto rev = lg::revision();
        check(lg::startCheck() && lg::busy(), "la verification part sur un fil");
        for (int i = 0; i < 1000 && lg::busy(); ++i) {
            lg::poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const auto last = lg::lastResult();
        g_said.push_back(last.message);
        check(!lg::busy() && lg::revision() == rev + 1 && last.job == lg::Job::Check && last.licenseInstalled, "releve par poll : " + last.message);
        check(lic::getLicenseState().licenseId == "XPG-PAS-0002" && app::licence::badge().tone == 1, "la licence en vigueur : XPG-PAS-0002");
        const auto messages = app::licence::takeMessages();
        check(messages.size() == 1 && has(messages[0], "Ta licence XPG-PAS-0002 est install"), "le message de la barre d'etat");
        bool bell = false;
        for (const auto& n : app::bgtasks::notices()) bell = bell || (n.key == "licence:passerelle" && has(n.title, "XPG-PAS-0002") && n.action == "help.licence");
        check(bell, "l'avis de la cloche (bouton Licence... -> la fenetre Licence)");
        lgw::StatusContent removed;
        removed.message = "Remplace par un autre PC (essai)";
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 9, lgw::State::Removed, "", "", pk, removed));
        r = keep(lg::check());
        check(r.ok && has(r.notice, "retir\xC3\xA9 de la licence XPG-PAS-0002") && lg::loadRecord(rec, &why) && rec.lastState == "retiree" && rec.lastNumber == 9,
              "retire de la licence : note, un avis (" + r.message + ")");
        check(lg::startCheck(), "une autre verification");
        lg::shutdown();
        check(!lg::busy(), "shutdown : le fil attendu, rien ne reste en cours");
    }

    std::printf("-- un etat rejoue pour une autre demande ne change pas la demande en cours\n");
    const std::string oldNonce = rec.nonce;   // la demande n\xC2\xB0 4, acceptee
    {
        auto r = keep(lg::send(form("Poste essai A", "XPG-PAS-0002")));
        check(r.ok && fake.issues.size() == 5 && lg::loadRecord(rec, &why) && lg::pending(rec) && rec.nonce != oldNonce, "une nouvelle demande (" + r.message + ")");
        lgw::StatusContent none;
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 10, lgw::State::Received, rec.nonce, "", pk, none));
        r = keep(lg::check());
        check(r.ok && lg::loadRecord(rec, &why) && has(lg::stateText(rec), "Re\xC3\xA7ue par l'\xC3\xA9" "diteur") && rec.lastNumber == 10,
              "recue pour CETTE demande : Recue par l'editeur (" + lg::stateText(rec) + ")");
        const std::string shown = lg::stateText(rec), lastMessage = rec.lastMessage;
        const int histBefore = countPasserelle();
        struct Replay { std::uint64_t number; lgw::State state; std::string nonce; const char* what; };
        const Replay replays[] = {
            {11, lgw::State::Received, oldNonce, "recue pour l'ancienne demande"},
            {12, lgw::State::Cancelled, oldNonce, "annulee pour l'ancienne demande"},
            {13, lgw::State::Cancelled, "", "annulee sans demande"},
            {14, lgw::State::Refused, oldNonce, "refusee pour l'ancienne demande"},
            {15, lgw::State::Received, "", "recue sans demande"},
        };
        for (const auto& x : replays) {
            lgw::StatusContent c;
            if (x.state == lgw::State::Refused) c.message = "Raison d'essai d'un ancien refus";
            fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, x.number, x.state, x.nonce, "", pk, c));
            r = keep(lg::check());
            check(r.ok && !r.refused && r.notice.empty() && has(r.message, "ne change pas") && lg::loadRecord(rec, &why) && lg::pending(rec) &&
                      rec.outcome.empty() && lg::stateText(rec) == shown && rec.lastMessage == lastMessage && rec.lastNumber == x.number,
                  std::string(x.what) + " : la demande en cours ne change pas, l'etat est traite (" + r.message + " / " + lg::stateText(rec) + ")");
        }
        check(countPasserelle() == histBefore + 5, "chaque etat rejoue est note dans l'historique");
        lgw::StatusContent c;
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 16, lgw::State::Cancelled, rec.nonce, "", pk, c));
        r = keep(lg::check());
        check(r.ok && lg::loadRecord(rec, &why) && !lg::pending(rec) && rec.outcome == "annulee", "annulee pour CETTE demande : notee (" + r.message + ")");
    }

    std::printf("-- une licence qui ne s'installe pas est reprise\n");
    {
        auto r = keep(lg::send(form("Poste essai A")));
        check(r.ok && fake.issues.size() == 6 && lg::loadRecord(rec, &why) && lg::pending(rec), "une nouvelle demande (" + r.message + ")");
        // Une licence liee a un AUTRE PC (sa demande, signee par sa cle) : le magasin la refuse.
        lic::ActivationRequest oq;
        oq.deviceKey = otherPc->publicKeyHex();
        oq.deviceName = "Poste essai B";
        oq.createdAt = lic::systemNow();
        oq.nonce = "0123456789abcdef0123456789abcdef";
        const std::string otherReq = lic::writeBlock(lic::signBlock(lic::DocKind::Request, lic::writeRequest(oq), otherPc->publicKey(),
                                                                    [&](std::string_view m) { return otherPc->sign(m); }));
        lgw::StatusContent bad;
        bad.license = licenceFor(*key, registry, "XPG-PAS-0003", otherReq);
        check(!bad.license.empty(), "une licence pour l'autre PC");
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 17, lgw::State::Accepted, rec.nonce, "", pk, bad));
        const int histBefore = countPasserelle();
        r = keep(lg::check());
        check(r.ok && !r.licenseInstalled && r.retryLater && has(r.message, "autre PC") && has(r.notice, "Nouvel essai") && r.noticeTone == "error",
              "acceptee, mais la licence est pour un autre PC : pas installee, a reprendre (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && rec.lastNumber == 16 && lg::pending(rec) && rec.outcome.empty() && has(rec.lastMessage, "autre PC"),
              "le releve : l'etat n\xC2\xB0 17 pas traite, la demande attend toujours, la raison gardee");
        check(lic::getLicenseState().licenseId == "XPG-PAS-0002", "la licence en place reste");
        r = keep(lg::check());
        check(r.ok && r.retryLater && r.notice.empty() && has(r.message, "autre PC") && countPasserelle() == histBefore + 1 && lg::loadRecord(rec, &why) &&
                  rec.lastNumber == 16,
              "verifier encore : repris (pas de nouvel avis, pas de bruit dans l'historique)");
        lgw::OnlineRequest q;
        lic::ActivationRequest req;
        std::string device;
        check(openIssue(*key, fake.issues.back(), q, req, device, why), "l'editeur ouvre la demande");
        lgw::StatusContent good;
        good.license = licenceFor(*key, registry, "XPG-PAS-0003", q.request);
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 18, lgw::State::Accepted, rec.nonce, "", pk, good));
        r = keep(lg::check());
        check(r.ok && r.licenseInstalled && !r.retryLater && has(r.notice, "Ta licence XPG-PAS-0003 est install") && lic::getLicenseState().licenseId == "XPG-PAS-0003",
              "puis la bonne licence : installee (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && rec.lastNumber == 18 && rec.outcome == "acceptee" && rec.receivedLicense == "XPG-PAS-0003",
              "le releve : acceptee, n\xC2\xB0 18");
    }

    std::printf("-- la liste de revocation scellee dans l'etat\n");
    {
        std::string revocations;
        check(adm::signRevocations(*key, "{\"number\": 3, \"revoked\": [{\"license_id\": \"XPG-PAS-0003\", \"reason\": \"Fin du contrat d'essai\"}]}",
                                   lic::systemNow(), revocations, &why),
              "la liste n\xC2\xB0 3 signee (" + why + ")");
        lgw::StatusContent content;
        content.revocations = revocations;
        content.message = "Licence revoquee (essai)";
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 19, lgw::State::Revoked, "", "", pk, content));
        check(!fake.anyPublicContains("Fin du contrat") && !fake.anyPublicContains("XPG-PAS-0003"), "la liste et sa raison ne sont pas en clair dans le depot");
        const auto r = keep(lg::check());
        const auto st = lic::getLicenseState();
        check(r.ok && st.status == lic::LicenseStatus::Revoked && st.revocationNumber == 3 && st.licenseId == "XPG-PAS-0003",
              "la licence est Revoquee sur ce PC (" + r.message + " / " + st.message + ")");
        check(has(r.notice, "r\xC3\xA9voqu\xC3\xA9 la licence XPG-PAS-0003") && has(r.message, "n\xC2\xB0 3 est install"), "l'avis dit la licence installee : " + r.notice);
        check(lg::loadRecord(rec, &why) && rec.lastNumber == 19 && rec.lastState == "revoquee", "le releve : revoquee, n\xC2\xB0 19");
    }

    std::printf("-- deux Studios sur le meme dossier\n");
    {
        // La fusion : la demande la plus recente, le plus grand numero traite, une annulation de l'un ou de l'autre.
        lg::Record older, newer;
        older.issue = 3;
        older.nonce = "aaaa";
        older.sentAt = 1000;
        older.local = lg::kLocalSent;
        older.outcome = "acceptee";
        older.lastNumber = 9;
        older.lastMessage = "neuf";
        older.configNumber = 4;
        newer.issue = 4;
        newer.nonce = "bbbb";
        newer.sentAt = 2000;
        newer.local = lg::kLocalSent;
        newer.lastNumber = 7;
        newer.configNumber = 2;
        auto m = lg::mergeRecords(newer, older);
        check(m.issue == 4 && m.nonce == "bbbb" && lg::pending(m) && m.lastNumber == 9 && m.lastMessage == "neuf" && m.configNumber == 4,
              "la demande la plus recente (sur le disque), le plus grand numero (de ce travail)");
        m = lg::mergeRecords(older, newer);
        check(m.issue == 4 && lg::pending(m) && m.lastNumber == 9 && m.lastMessage == "neuf" && m.configNumber == 4, "dans l'autre sens aussi");
        lg::Record cancelled = newer;
        cancelled.local = lg::kLocalCancelled;
        cancelled.cancelledAt = 2100;
        m = lg::mergeRecords(cancelled, newer);
        check(m.local == lg::kLocalCancelled && m.cancelledAt == 2100 && !lg::pending(m), "la meme demande : l'annulation de l'autre reste");

        // Pour de vrai : l'autre Studio envoie sa demande PENDANT la verification de celui-ci.
        g_hook = [&](const app::ai::HttpRequest& req) {
            if (!has(req.url, lgw::statusPath(myId))) return false;
            const auto other = keep(lg::send(form("Poste essai A")));
            check(other.ok, "l'autre Studio envoie sa demande (" + other.message + ")");
            return true;
        };
        app::ai::setHttpFactory([] { return std::make_unique<HookClient>(); });
        auto r = keep(lg::check());
        check(r.ok && !g_hook && fake.issues.size() == 7, "la verification finit (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && lg::pending(rec) && rec.issue == 7 && rec.lastNumber == 19,
              "la demande envoyee par l'autre Studio n'est pas perdue (n\xC2\xB0 " + std::to_string(rec.issue) + ")");
        r = keep(lg::send(form("Poste essai A")));
        check(!r.ok && fake.issues.size() == 7, "une seconde demande reste refusee (" + r.message + ")");

        // Et l'inverse : l'autre Studio traite un nouvel etat PENDANT l'envoi de celui-ci.
        r = keep(lg::cancel());
        check(r.ok && lg::loadRecord(rec, &why) && !lg::pending(rec), "la demande annulee (" + r.message + ")");
        lgw::StatusContent c;
        fake.files[lgw::statusPath(myId)] = signedStatus(*key, status(myId, 20, lgw::State::Cancelled, rec.nonce, "", pk, c));
        g_hook = [&](const app::ai::HttpRequest& req) {
            if (req.method != "POST" || !has(req.url, "/issues") || has(req.url, "/comments")) return false;
            const auto other = keep(lg::check());
            check(other.ok && has(other.message, "pris note de l'annulation"), "l'autre Studio traite l'etat n\xC2\xB0 20 (" + other.message + ")");
            return true;
        };
        r = keep(lg::send(form("Poste essai A")));
        check(r.ok && !g_hook && fake.issues.size() == 8, "la demande part (" + r.message + ")");
        check(lg::loadRecord(rec, &why) && lg::pending(rec) && rec.issue == 8 && rec.lastNumber == 20 && rec.lastState == "annulee",
              "le releve garde la demande ET l'etat n\xC2\xB0 20 traite par l'autre Studio (dernier n\xC2\xB0 " + std::to_string(rec.lastNumber) + ")");
        r = keep(lg::check());
        check(r.ok && has(r.message, "Rien de nouveau"), "l'etat n\xC2\xB0 20 n'est pas traite deux fois (" + r.message + ")");
        g_hook = nullptr;
        fakegh::install(&fake);
    }

    std::printf("-- le jeton des demandes ne sort jamais\n");
    {
        bool inUrl = false;
        for (const auto& line : fake.requests) inUrl = inUrl || has(line, "essai_jeton_");
        check(!inUrl, "jamais dans une adresse");
        bool inMessage = false;
        for (const auto& m : g_said) inMessage = inMessage || has(m, "essai_jeton_");
        check(!inMessage, "jamais dans un message (" + std::to_string(g_said.size()) + ")");
        bool inHistory = false;
        for (const auto& h : lic::getHistory(0)) inHistory = inHistory || has(h.text, "essai_jeton_");
        check(!inHistory, "jamais dans l'historique");
        check(!has(readFile(base / "pc" / lg::kRecordFile), "essai_jeton_") && !has(readFile(base / "pc" / "historique.jsonl"), "essai_jeton_"),
              "jamais dans le releve local ni dans historique.jsonl");
        bool bodyToken = false;
        for (const auto& w : fake.writes) bodyToken = bodyToken || has(w, "essai_jeton_");
        check(!bodyToken, "jamais dans le corps d'une requete");
    }

    fakegh::install(nullptr);
    lg::setAddressForTests(std::nullopt);
    fs::remove_all(base, ec);
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
