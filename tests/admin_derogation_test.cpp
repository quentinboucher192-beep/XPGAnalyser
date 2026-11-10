// =============================================================================
//  tests/admin_derogation_test.cpp - 1.47.0 : LA CONNEXION AU SERVICE QUI
//  VOYAGE AVEC L'INSTALLATEUR ET LA DEROGATION << Gerer le service >>
//  (docs/ADMINS.md, par. 11 ; license_admin/ServiceConnection, gui/WorkspaceDerogation.cpp)
// -----------------------------------------------------------------------------
//  Sans reseau : la connexion signee (le certificat du service, signe par la cle
//  de l'editeur) lue, une adresse modifiee refusee, un fichier sans signature
//  refuse, une cle inconnue refusee ; la recherche (a cote du programme, une copie
//  renommee par un navigateur, le dossier des licences d'un Studio) ; un poste neuf
//  la prend au chargement, l'admin+ jamais ; les phrases du compte a rebours.
//  Contre le VRAI Worker sur ce poste (tests/service_local.hpp) : l'admin+ verifie
//  et exporte sa connexion ; un poste neuf la prend, demande a devenir admin, est
//  accepte ; il voit le service en lecture seule (le service lui refuse tout geste) ;
//  une derogation d'1 h : signee, son document verifie, les gestes permis (le
//  journal le dit, l'admin+ est prevenu), cle-publique.txt = les cles de l'editeur
//  (jamais la sienne) ; revoquee : refusee aussitot ; une autre, puis l'horloge du
//  service avancee d'1 h 02 : finie, refusee, la ligne << derogation_expiree >>.
//  Aucune vraie cle, aucun vrai client : des cles creees ici, des noms d'essai.
//
//    admin_derogation_test SOURCE
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/ServiceConnection.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace lic = licensing;
namespace sv = licensing::service;
namespace la = license_admin;
namespace cx = license_admin::connexion;
namespace gui = license_admin::gui;
namespace online = license_admin::online;

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
void pin(const fs::path& folder, const std::string& editorHex) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editorHex + "\"]}\n");
}

// Un certificat de service signe par `key` (la connexion), pour `url`.
std::string certFor(const lic::SigningKey& key, const std::string& url, const std::string& serviceKey, lic::Seconds now) {
    sv::ServiceCert c;
    c.key = serviceKey;
    c.url = url;
    c.issuedAt = now - 60;
    c.expiresAt = now + 365 * 86400;
    c.rights = {sv::kRightActivation, sv::kRightFloating, sv::kRightTime, sv::kRightTrial};
    return lic::writeBlock(lic::signBlock(lic::DocKind::ServiceCert, sv::writeCert(c), key.publicKey(), [&key](std::string_view m) { return key.sign(m); }));
}

const gui::JournalRow* rowOf(const gui::JournalView& v, const std::string& action) {
    for (const auto& r : v.rows)
        if (r.line.parsed.action == action) return &r;
    return nullptr;
}
int countOf(const gui::JournalView& v, const std::string& action) {
    int n = 0;
    for (const auto& r : v.rows) n += r.line.parsed.action == action ? 1 : 0;
    return n;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : std::string(".");
    (void)source;
    if (!lic::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-derogation-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
            cx::setPlaces({});
        }
    } cleanup{root};
    const lic::Seconds t0 = lic::systemNow();
    const std::string fakeServiceKey(64, 'a');

    // ======================================================================== sans reseau
    std::printf("-- la connexion signee, sans reseau\n");
    gui::Workspace plus(U(root / "plus"));
    check(plus.createKey("essai derogation", true).ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+");
    std::string why;
    auto intruder = lic::SigningKey::create(U(root / "intrus" / "cle.cle"), la::publisherKeySpec(), "Intrus d'essai", true, &why);
    check(intruder != nullptr, "une autre cle (un intrus)");
    const std::vector<lic::crypto::PublicKey> editors{plus.signingKey()->publicKey()};
    const std::string good = certFor(*plus.signingKey(), "https://xpg-licences.essai.workers.dev", fakeServiceKey, t0);
    {
        cx::Connection c;
        check(cx::read(good, editors, t0, c, &why) && c.url == "https://xpg-licences.essai.workers.dev" && c.serviceKey == fakeServiceKey &&
                  c.editorKeyId == plus.signingKey()->keyId() && !c.expired,
              "un certificat signe par la cle de l'editeur : l'adresse et la cle du service (" + why + ")");
        std::string tampered = good;
        const auto at = tampered.find("essai.workers.dev");
        tampered.replace(at, 5, "pirat");   // la meme taille : seule la signature ne va plus
        check(!cx::read(tampered, editors, t0, c, &why) && has(why, "modifi"), "une adresse modifiee : refusee (" + why + ")");
        check(!cx::read("{\"url\":\"https://xpg-licences.essai.workers.dev\"}", editors, t0, c, &why) && has(why, "pas une connexion sign"),
              "une adresse sans signature : refusee (" + why + ")");
        const std::string other = certFor(*intruder, "https://xpg-licences.pirate.workers.dev", fakeServiceKey, t0);
        check(!cx::read(other, editors, t0, c, &why) && has(why, "n'est pas celle de l'"), "signee par une cle inconnue : refusee (" + why + ")");
        check(cx::read(good, editors, t0 + 400 * 86400, c, &why) && c.expired, "un certificat passe : l'adresse reste, marquee a renouveler");
    }
    std::printf("-- ou la chercher : a cote du programme, le Studio de ce PC\n");
    {
        const fs::path beside = root / "programme", studio = root / "studio";
        writeFile(beside / "connexion-service (1).xpgsvc", good);   // une copie renommee par un navigateur
        cx::Places p;
        p.beside = {U(beside)};
        auto d = cx::discover(p, editors, t0);
        check(d.found && d.connection.url == "https://xpg-licences.essai.workers.dev" && has(d.connection.source, "programme"),
              "a cote du programme (une copie renommee) : trouvee (" + d.connection.source + ")");
        writeFile(studio / "decouverte.xpgdec", good);
        writeFile(studio / "registre.xpgreg", "pas un bloc");
        cx::Places q;
        q.studioFolder = U(studio);
        d = cx::discover(q, editors, t0);
        check(d.found && has(d.connection.source, "Studio") && has(d.connection.source, "decouverte.xpgdec"), "le dossier des licences du Studio : trouvee");
        fs::remove(studio / "decouverte.xpgdec", ec);
        writeFile(studio / "activation.xpgact", "XPG-LICENSING 1 activation 2\n{}\n");
        d = cx::discover(q, editors, t0);
        check(!d.found && d.refused.empty(), "un fichier du Studio sans certificat : rien, sans refus");
        // Un poste neuf : la connexion prise au chargement ; une modifiee a cote : refusee, dite.
        cx::setPlaces(p);
        pin(root / "neuf", plus.signingKey()->publicKeyHex());
        gui::Workspace neuf(U(root / "neuf"));
        check(neuf.service().configured && neuf.service().config.url == "https://xpg-licences.essai.workers.dev" && neuf.service().hasConnection &&
                  neuf.service().connection.serviceKey == fakeServiceKey && fs::exists(root / "neuf" / cx::kFileName),
              "un poste neuf : l'adresse prise de la connexion signee, sa cle de service retenue");
        check(!plus.service().configured, "l'admin+ ne prend jamais une connexion (il regle son service)");
        plus.reload();
        check(!plus.service().configured, "l'admin+, relu : toujours rien de regle");
        const fs::path bad = root / "programme-mauvais";
        std::string tampered = good;
        tampered.replace(tampered.find("essai.workers.dev"), 5, "pirat");
        writeFile(bad / "connexion-service.xpgsvc", tampered);
        cx::Places pb;
        pb.beside = {U(bad)};
        cx::setPlaces(pb);
        pin(root / "neuf2", plus.signingKey()->publicKeyHex());
        gui::Workspace neuf2(U(root / "neuf2"));
        check(!neuf2.service().configured && neuf2.service().connectionRefused.size() == 1 && has(neuf2.service().connectionRefused[0].why, "modifi"),
              "une connexion modifiee a cote du programme : refusee, et le poste le dit (" +
                  (neuf2.service().connectionRefused.empty() ? std::string() : neuf2.service().connectionRefused[0].why) + ")");
        gui::Workspace neuf3(U(root / "neuf3"));   // la cle de l'editeur pas connue de ce poste
        check(!neuf3.service().configured, "un poste qui ne connait pas la cle de l'editeur : rien n'est pris");
        cx::setPlaces({});
    }
    std::printf("-- la derogation : les phrases, l'heure du service\n");
    {
        check(gui::remainingText(3000) == "50 min" && gui::remainingText(3 * 3600 + 5 * 60) == "3 h 05" && gui::remainingText(45) == "45 s",
              "le temps qui reste : 50 min, 3 h 05, 45 s");
        gui::Derogation d;
        d.id = std::string(32, '1');
        d.adminKey = std::string(64, 'b');
        d.start = 1000;
        d.end = 1000 + 3600;
        d.serverNow = 1000 + 600;   // le service avait 10 min de plus que ...
        d.readAt = 5000;            // ... l'heure de ce poste a la lecture
        check(d.activeAt(5000) && d.remaining(5000) == 3000 && d.remaining(5000 + 2999) == 1 && !d.activeAt(5000 + 3000),
              "en cours a l'heure du SERVICE (l'ecart vu a la lecture), finie d'elle-meme a la fin");
        d.revoked = 1700;
        check(!d.activeAt(5000), "revoquee : plus en cours");
    }

    // ======================================================================== le service local
    xpg_test::LocalService svc({plus.signingKey()->publicKeyHex()});
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    if (!svc.ready()) {
        std::printf("ECHEC : le service local : %s\n", svc.error().c_str());
        return 1;
    }
    std::printf("   service local : %s (%.1f s)\n", svc.url().c_str(), svc.startSeconds());

    std::printf("-- l'admin+ : son service, la connexion a exporter\n");
    const fs::path exported = root / "envoi" / cx::kFileName;
    {
        check(plus.serviceConfigure(svc.url()).ok, "l'adresse du service");
        check(plus.serviceCertify({}).ok, "la cle du service certifiee");
        check(!plus.exportConnection(U(exported)).ok, "rien a exporter avant Verifier");
        online::ServiceState st;
        check(plus.serviceVerify(&st).ok && st.certOk && !st.certBlock.empty(), "Verifier : le certificat signe, sa copie");
        plus.adoptServiceState(st);
        fs::create_directories(exported.parent_path(), ec);
        const auto e = plus.exportConnection(U(exported));
        check(e.ok && fs::exists(exported), "Exporter la connexion (" + e.text() + ")");
        const std::string text = readFile(exported);
        check(!has(text, "PRIVATE") && !has(text, "secret") && has(text, "XPG-LICENSING 1 service"), "rien de secret : le certificat du service seul");
        auto r = plus.signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json");
        check(r.ok, "le registre d'exemple signe");
        auto job = plus.takeServiceJob();
        if (job) (void)plus.serviceRun(*job);
        std::string code;
        check(plus.syncKeyCreate(&code).ok, "la cle de synchro");
    }

    std::printf("-- le futur admin : l'Admin installe, la connexion recue, la demande\n");
    gui::Workspace lea(U(root / "lea"));
    std::string fp;
    {
        check(!lea.service().configured, "l'Admin neuf : pas encore d'adresse");
        auto u = lea.useConnectionFile(U(exported));
        check(!u.ok && has(u.message, "n'est pas celle de l'"), "sans la cle de l'editeur compilee : la connexion est refusee (" + u.message + ")");
        pin(root / "lea", plus.signingKey()->publicKeyHex());   // (l'Admin livre a la cle de l'editeur compilee : PublisherKey.hpp)
        lea.reload();
        std::string modified = readFile(exported);
        const auto port = modified.find(":" + std::to_string(svc.port()));
        modified[port + 1] = modified[port + 1] == '9' ? '8' : static_cast<char>(modified[port + 1] + 1);
        writeFile(root / "modifiee.xpgsvc", modified);
        u = lea.useConnectionFile(U(root / "modifiee.xpgsvc"));
        check(!u.ok && has(u.message, "modifi") && !lea.service().configured, "une connexion au port modifie : refusee (" + u.message + ")");
        u = lea.useConnectionFile(U(exported));
        check(u.ok && lea.service().configured && lea.service().config.url == svc.url() && lea.service().hasConnection, "la connexion signee : connecte (" + u.text() + ")");
        const auto r = lea.requestAdmin("L\xC3\xA9" "a Essai", "lea.essai@exemple.invalid", &fp);
        check(r.ok && !fp.empty(), "la demande pour devenir admin part tout de suite (" + r.text() + ")");
        lea.reload();
        check(lea.role() == gui::Role::Pending && lea.adminKey().fingerprint == fp, "en attente : sa cle d'admin, son empreinte");
        const auto acc = plus.acceptAdmin(lea.adminKey().keyHex, licensing::admins::kAllRightsProfile);
        check(acc.ok, "l'admin+ l'accepte (" + acc.text() + ")");
        plus.reload();
        gui::AdminRefresh x;
        (void)lea.refreshAdminState(nullptr, &x);
        lea.reload();
        check(lea.role() == gui::Role::Admin, "Lea est admin");
        online::ServiceState st;
        check(lea.serviceVerify(&st).ok && st.serviceKey == lea.service().connection.serviceKey,
              "le service a bien la cle que nommait la connexion signee");
    }

    std::printf("-- un admin : le service en lecture seule\n");
    {
        check(!lea.mayManageService() && has(lea.whyNotService(), "admin+ seul g"), "gerer le service : " + lea.whyNotService());
        const auto u = lea.useDerogation("deployer");
        check(!u.ok && has(u.message, "admin+ seul g"), "le service refuse le geste (" + u.message + ")");
        check(!lea.serviceConfigure("https://autre.essai.workers.dev").ok && lea.service().config.url == svc.url(), "changer l'adresse : refuse");
        check(!lea.grantDerogation(lea.adminKey().keyHex, 1).ok, "un admin ne s'accorde rien");
        gui::Derogation d;
        check(lea.readDerogation(d).ok && !d.exists(), "aucune derogation");
    }

    std::printf("-- la derogation d'1 h : accordee, verifiee, utilisee\n");
    {
        check(!plus.grantDerogation(lea.adminKey().keyHex, 5).ok, "5 h : refuse (1, 4, 24 ou 72)");
        check(!plus.grantDerogation(lea.adminKey().keyHex, 0).ok, "permanente (0) : refuse");
        gui::Derogation g;
        const auto a = plus.grantDerogation(lea.adminKey().keyHex, 1, &g);
        check(a.ok && g.exists() && g.end - g.start == 3600 && g.adminKey == lea.adminKey().keyHex && has(a.text(), "Cloudflare") && has(a.text(), "jeton partag"),
              "accordee pour 1 h (" + a.message + ")");
        gui::Derogation d;
        const auto r = lea.readDerogation(d);
        check(r.ok && d.signatureOk && d.activeAt(lea.now()) && d.remaining(lea.now()) > 3500, "Lea la lit : signee par la cle de l'editeur, en cours (" + r.message + ")");
        lea.adoptDerogation(d);
        check(lea.mayManageService() && lea.whyNotService().empty(), "Lea gere le service");
        online::AdminWatch w;
        check(lea.serviceWatch(w).ok && w.derogationSaid && w.derogationState == "active" && w.derogationAdmin == lea.adminKey().keyHex,
              "le regard des 15 s la dit (GET /admin/etat)");
        const auto u = lea.useDerogation("deployer", "service-deployer --essai");
        check(u.ok && has(u.message, "journal"), "le geste << deployer >> : permis par le service (" + u.message + ")");
        check(lea.serviceConfigure(svc.url()).ok, "l'adresse : permise");
        const fs::path kit = root / "kit";
        fs::create_directories(kit, ec);
        const auto k = lea.writeEditorKeysFile(U(kit));
        const std::string pub = readFile(kit / "cle-publique.txt");
        check(k.ok && has(pub, plus.signingKey()->publicKeyHex()) && !has(pub, lea.adminKey().keyHex), "cle-publique.txt : la cle de l'editeur, jamais la sienne");
        check(!lea.revokeDerogation().ok, "Lea ne revoque rien");
        int added = 0;
        check(plus.fetchJournal(&added).ok, "l'admin+ releve le journal");
        const auto v = plus.journalLines();
        const auto* granted = rowOf(v, "derogation_accorder");
        const auto* used = rowOf(v, "derogation_utilisee");
        check(granted && granted->verified && granted->who == "admin+", "au journal : << derogation_accorder >> (l'admin+)");
        check(used && used->verified && has(used->line.parsed.summary, "deployer"), "au journal : << derogation_utilisee >> (le service)");
        bool alerted = false;
        for (const auto& al : plus.newAlerts()) alerted = alerted || al.line.parsed.action == "derogation_utilisee";
        check(alerted, "l'admin+ est prevenu tout de suite de l'usage");
    }

    std::printf("-- revoquee avant la fin\n");
    {
        gui::Derogation d;
        const auto r = plus.revokeDerogation(&d);
        check(r.ok && d.state == "revoquee", "revoquee (" + r.message + ")");
        const auto u = lea.useDerogation("deployer");
        check(!u.ok && has(u.message, "voqu"), "le geste suivant : refuse (" + u.message + ")");
        online::AdminWatch w;
        check(lea.serviceWatch(w).ok && w.derogationState == "revoquee", "le regard : revoquee");
        lea.adoptDerogation(w);
        check(!lea.mayManageService() && has(lea.whyNotService(), "voqu"), "Lea : lecture seule (" + lea.whyNotService() + ")");
        (void)plus.fetchJournal(nullptr);
        check(rowOf(plus.journalLines(), "derogation_revoquer") != nullptr, "au journal : << derogation_revoquer >>");
    }

    std::printf("-- une autre, puis sa fin a l'heure du service\n");
    {
        check(plus.grantDerogation(lea.adminKey().keyHex, 1).ok, "une nouvelle derogation d'1 h");
        online::AdminWatch w;
        check(lea.serviceWatch(w).ok && w.derogationState == "active", "en cours");
        lea.adoptDerogation(w);
        check(lea.mayManageService(), "Lea gere de nouveau le service");
        check(svc.restart(3720), "l'horloge du service avancee d'1 h 02 (le meme etat)");
        const auto u = lea.useDerogation("deployer");
        check(!u.ok && has(u.message, "pris fin"), "finie : refusee par le service (" + u.message + ")");
        check(lea.serviceWatch(w).ok && w.derogationState == "expiree", "le regard : finie");
        lea.adoptDerogation(w);
        check(!lea.mayManageService() && has(lea.whyNotService(), "termin"), "Lea : de nouveau en lecture seule (" + lea.whyNotService() + ")");
        (void)plus.fetchJournal(nullptr);
        check(countOf(plus.journalLines(), "derogation_expiree") == 1, "au journal : << derogation_expiree >>, une fois");
        check(!plus.revokeDerogation().ok, "revoquer une derogation finie : rien a faire");
    }
    return finish();
}
