// =============================================================================
//  tests/admins_admin_test.cpp - 1.44.0 : PLUSIEURS ADMINS DANS L'OUTIL DE
//  L'EDITEUR (license_admin/gui/Workspace : WorkspaceAdmins.cpp), contre le VRAI
//  Worker lance sur ce poste (tests/service_local.hpp)
// -----------------------------------------------------------------------------
//  L'admin+ (une cle d'ESSAI de l'editeur) et un second poste (un autre dossier)
//  qui demande a devenir admin : la meme empreinte des deux cotes, l'acceptation
//  avec un profil, le role << admin >>, la cle de synchro recue par le trousseau,
//  une licence signee par l'admin (le fichier porte la liste des admins d'abord)
//  qu'un PC client (qui ne croit que la cle de l'editeur) installe, valide ; un
//  droit qui manque (les offres) : refuse ici, et 403 par le service si on force ;
//  le journal des admins (les lignes des deux, relevees et verifiees par l'admin+ :
//  la chaine ; une copie modifiee se voit) ; les profils (et leur synchro par
//  l'archive) ; un droit retire (ce qu'il a signe reste valable : gardes) ; le
//  retrait (sa licence reste valable chez le client, une nouvelle est refusee, le
//  service lui repond 403 retire ; le registre qu'il avait signe est re-signe par
//  la cle de l'editeur) ; une demande scellee pour un autre admin ; le jeton d'admin.
//  Aucune vraie cle, aucun vrai client : des cles creees ici, des noms d'essai.
//
//    admins_admin_test SOURCE
//  Sans Node.js ni wrangler : la partie sans reseau, puis "SAUTE : ...".
// =============================================================================
#include "service_local.hpp"

#include "../src/app/ai/AiHttp.hpp"
#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ArchiveSync.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/license_admin/gui/Workspace.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/licensing/Store.hpp"

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
namespace adm = licensing::admins;
namespace sv = licensing::service;
namespace json = core::json;
namespace la = license_admin;
namespace on = license_admin::online;
namespace ar = license_admin::archive;
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
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
int finish() {
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

const std::string kLea = "L\xC3\xA9" "a Essai";
const std::string kCustomer = "Atelier d'essai";

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

// Un PC client : il ne croit que la cle de l'editeur (d'essai), rien de compile.
lic::StoreOptions clientOptions(const fs::path& folder, const lic::SigningKey& editor) {
    lic::StoreOptions o;
    o.folder = U(folder);
    o.compiledKeys = false;
    lic::TrustedKey t;
    t.id = editor.keyId();
    t.name = editor.name();
    t.key = editor.publicKey();
    t.test = editor.test();
    o.trustedKeys = {t};
    o.machineId = "machine-essai-client";
    return o;
}

const gui::JournalRow* rowOf(const gui::JournalView& v, const std::string& action, const std::string& who = {}) {
    for (const auto& r : v.rows)
        if (r.line.parsed.action == action && (who.empty() || r.who == who)) return &r;
    return nullptr;
}

std::string problemsOf(const gui::JournalView& v) {
    std::string out;
    for (const auto& p : v.problems) out += " | " + p;
    return out;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : std::string(".");
    if (!lic::crypto::init()) {
        std::printf("libsodium ne demarre pas\n");
        return 1;
    }
    std::mt19937_64 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("xpg-admins-admin-" + std::to_string(rng() % 1000000000ULL));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sortie", ec);
    struct Cleanup {
        fs::path p;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(p, e);
        }
    } cleanup{root};

    // ======================================================================== sans reseau
    std::printf("-- les roles, sans reseau\n");
    gui::Workspace plus(U(root / "plus"));
    {
        check(plus.role() == gui::Role::None && !plus.signer() && plus.myRights().empty(), "un poste neuf : aucun role, rien ne signe");
        check(!plus.whyNot("licence_creer").empty(), "un poste neuf : le bouton dit pourquoi (" + plus.whyNot("licence_creer") + ")");
        const auto c = plus.createKey("essai admins", true);
        check(c.ok && plus.role() == gui::Role::Plus, "la cle de l'editeur (d'essai) : l'admin+ (" + c.text() + ")");
        check(plus.signer() == plus.signingKey() && plus.myRights().size() == 13 && plus.can("offres") && plus.whyNot("offres").empty(),
              "l'admin+ : sa cle signe, les 13 droits");
        const auto r = plus.signRegistryJson(source + "/tests/fixtures/licences/registre-exemple.json");
        check(r.ok && plus.hasRegistry(), "le registre d'exemple signe (" + r.text() + ")");
        check(gui::detail::registryRights(&plus.registry(), plus.registry()).empty(), "le meme registre : aucune partie ne change");
        lic::Registry edited = plus.registry();
        edited.editions.front().price = "1 \xE2\x82\xAC HT (essai)";
        check(gui::detail::registryRights(&plus.registry(), edited) == std::vector<std::string>{"offres"}, "un prix change : le droit << offres >>");
        if (!edited.options.empty()) {
            lic::Registry o2 = plus.registry();
            o2.options.front().label += " (essai)";
            check(gui::detail::registryRights(&plus.registry(), o2) == std::vector<std::string>{"options"}, "une option change : le droit << options >>");
        }
        // le journal : une ligne signee par la cle qui signe, gardee pour l'envoi
        check(plus.journalWaiting() >= 1, "la signature du registre a ecrit sa ligne au journal a envoyer");
        check(!plus.journal("action_inconnue", "x", "y").ok, "une action inconnue du journal : refusee");
        // le jeton d'admin
        const auto t = plus.writeAdminToken(true);
        const std::string tok = readFile(root / "plus" / "jeton-admin.xpgjet");
        std::vector<lic::SignedBlock> blocks;
        adm::AdminToken token;
        check(t.ok && lic::readBlocks(tok, blocks, nullptr) && blocks.size() == 1 && blocks[0].kind == lic::DocKind::AdminToken &&
                  lic::verifyBlock(blocks[0], plus.signingKey()->publicKey()) && adm::parseAdminToken(blocks[0].document, token, nullptr) &&
                  token.key == plus.signingKey()->publicKeyHex() && token.machine.size() == 32 && token.version == gui::kAppVersion && token.name == "admin+" &&
                  token.expires > lic::systemNow() + 29 * 86400,
              "le jeton d'admin de l'admin+ : signe, sa cle, sa machine, 30 jours, la version");
    }

    std::printf("-- une demande scellee pour un autre admin (sans reseau)\n");
    {
        std::string why;
        auto other = lic::SigningKey::create(U(root / "autre" / "cle.cle"), la::adminKeySpec(), "Autre admin d'essai", false, &why);
        json::Value d = json::Value::object();
        d.set("numero", 7);
        d.set("poste", "0123456789abcdef0123456789abcdef");
        d.set("cle", "xpg-0000000000000000");
        d.set("scelle", json::Value());
        d.set("pour_un_autre", true);
        d.set("etat", "attente");
        d.set("cree", lic::isoTime(lic::systemNow()));
        d.set("maj", lic::isoTime(lic::systemNow()));
        json::Value list = json::Value::object();
        json::Value arr = json::Value::array();
        arr.push(d);
        list.set("demandes", arr);
        on::Options o;
        o.now = lic::systemNow();
        const auto r = other ? on::processRequests(U(root / "autre"), *other, list, "https://xpg-licences.essai.workers.dev", o) : on::Result{};
        const auto all = on::listRequests(U(root / "autre"));
        check(other && r.ok && all.size() == 1 && all[0].state == on::RequestState::Unreadable && all[0].forOther && !all[0].problems.empty() &&
                  has(all[0].problems.front(), "autre admin"),
              "listee << scellee pour un autre admin >>, illisible ici, rien ne casse (" + r.text() + ")");
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

    std::printf("-- l'admin+ : le service, le registre, la cle de synchro\n");
    {
        check(plus.serviceConfigure(svc.url()).ok, "l'adresse du service");
        const auto c = plus.serviceCertify({});
        check(c.ok, "la cle du service certifiee (" + c.text() + ")");
        // Le registre re-signe par l'application (une nouvelle emission, numero + 1)
        auto r = plus.signRegistry(plus.registry());
        check(r.ok, "le registre re-signe par l'application (" + r.text() + ")");
        auto job = plus.takeServiceJob();
        check(job && plus.serviceRun(*job).ok, "le registre depose");
        std::string code;
        check(plus.syncKeyCreate(&code).ok && !code.empty(), "la cle de synchro creee");
        const auto s = plus.archiveSync(false, false);
        check(s.ok, "l'archive envoyee (" + s.text() + ")");
        check(plus.journalWaiting() == 0, "le journal a envoyer est parti avec les appels au service");
    }

    std::printf("-- devenir admin\n");
    std::string fp;
    gui::Workspace leaWs(U(root / "lea2"));   // le poste de Lea : un autre dossier, aucune cle
    {
        check(leaWs.role() == gui::Role::None, "le poste de Lea : pas encore admin");
        check(leaWs.serviceConfigure(svc.url()).ok, "le poste de Lea : l'adresse du service");
        check(!leaWs.requestAdmin("", "lea@exemple.invalid", &fp).ok, "sans nom : refuse");
        const auto r = leaWs.requestAdmin(kLea, "lea@exemple.invalid", &fp);
        check(r.ok && !fp.empty(), "la demande pour devenir admin envoyee (" + r.text() + ")");
        leaWs.reload();
        check(leaWs.role() == gui::Role::Pending && leaWs.adminKey().present && leaWs.adminKey().fingerprint == fp,
              "en attente : sa cle d'admin, l'empreinte montree est la sienne");
        check(fp.size() == 29 && fp == adm::fingerprint(leaWs.adminKey().keyHex), "l'empreinte : 6 groupes de 4 (" + fp + ")");
        check(!leaWs.signer() && has(leaWs.whyNot("licence_creer"), "attend"), "en attente : rien ne signe (" + leaWs.whyNot("licence_creer") + ")");
        check(!leaWs.createKey("essai", true).ok, "un poste qui a une cle d'admin ne cree pas la cle de l'editeur");
        check(!leaWs.syncKeyCreate(nullptr).ok && !ar::hasSyncKey(U(root / "lea2")), "ni sa propre cle de synchro (celle de l'admin+ viendra)");
        std::string state;
        check(leaWs.adminRequestState(state).ok && state == "attente", "l'etat de la demande : attente");
        gui::AdminRefresh x;
        (void)leaWs.refreshAdminState(nullptr, &x);
        check(x.requestState == "attente" && !x.accepted, "le regard en attente : toujours en attente");
    }

    std::printf("-- l'admin+ accepte, avec un profil\n");
    {
        std::vector<gui::AdminRequest> reqs;
        const auto p = plus.pendingAdminRequests(reqs);
        check(p.ok && reqs.size() == 1 && reqs[0].fingerprint == fp && !reqs[0].fingerprintMismatch && reqs[0].name == kLea &&
                  reqs[0].key == leaWs.adminKey().keyHex && reqs[0].machine.size() == 32,
              "l'admin+ voit la demande, avec la MEME empreinte (" + p.text() + ")");
        check(!leaWs.pendingAdminRequests(reqs).ok, "un poste en attente ne voit pas les demandes d'admin");
        adm::Profile lv2;
        lv2.name = "Admin LV2";
        lv2.rights = {"licence_creer", "licence_prolonger", "demandes", "codes", "options"};
        check(plus.saveProfile(lv2).ok, "le profil << Admin LV2 >> (sans << offres >>)");
        adm::Profile all;
        all.name = adm::kAllRightsProfile;
        check(!plus.saveProfile(all).ok, "le profil << Tous les droits >> ne se modifie pas");
        check(!plus.acceptAdmin(leaWs.adminKey().keyHex, "Admin LV2", nullptr, false).ok, "sans << J'ai verifie l'empreinte >> : refuse");
        check(!plus.acceptAdmin(leaWs.adminKey().keyHex, "Profil inconnu").ok, "un profil inconnu : refuse");
        const auto a = plus.acceptAdmin(leaWs.adminKey().keyHex, "Admin LV2");
        check(a.ok && has(a.text(), "trousseau"), "accepte : la liste signee, publiee, la cle de synchro deposee (" + a.text() + ")");
        plus.reload();
        const adm::Admin* l = plus.adminList() ? plus.adminList()->byKey(leaWs.adminKey().keyHex) : nullptr;
        check(plus.adminList() && plus.adminList()->number == 1 && l && l->active() && l->profile == "Admin LV2" && l->rights.size() == 5 && l->name == kLea,
              "la liste n. 1 : Lea, son profil, ses 5 droits");
        check(!plus.acceptAdmin(leaWs.adminKey().keyHex, "Admin LV2").ok, "deja admin : plus de demande a accepter");
    }

    std::printf("-- l'admin : son role, ses droits, la cle de synchro par le trousseau\n");
    {
        gui::AdminRefresh x;
        const auto r = leaWs.refreshAdminState(nullptr, &x);
        check(x.accepted && x.syncKeyReceived && x.listAdopted, "le regard : acceptee, la liste gardee, la cle de synchro recue (" + r.text() + ")");
        leaWs.reload();
        check(leaWs.role() == gui::Role::Admin && leaWs.signer() && leaWs.signer()->keyId() == leaWs.adminKey().id && leaWs.myRights().size() == 5,
              "admin : sa cle signe, ses 5 droits");
        check(leaWs.can("licence_creer") && !leaWs.can("offres") && has(leaWs.whyNot("offres"), "Modifier les offres") && has(leaWs.whyNot("offres"), "admin+"),
              "le bouton grise dit le droit qui manque (" + leaWs.whyNot("offres") + ")");
        check(ar::hasSyncKey(U(root / "lea2")) && leaWs.service().syncKeyId == plus.service().syncKeyId, "la meme cle de synchro que l'admin+");
        const auto s = leaWs.archiveSync(true, false);
        leaWs.reload();
        check(s.ok && leaWs.hasRegistry() && leaWs.registry().number == plus.registry().number, "l'archive recue : le registre de l'editeur (" + s.text() + ")");
        check(!leaWs.saveProfile(adm::Profile{"Pas permis", {}, false, 0}).ok, "les profils : l'admin+ seulement");
        std::vector<gui::AdminRequest> reqs;
        check(!leaWs.pendingAdminRequests(reqs).ok && !leaWs.acceptAdmin(leaWs.adminKey().keyHex, "Admin LV2").ok, "gerer les admins : l'admin+ seulement");
        gui::AdminsView view;
        const auto v = leaWs.serviceAdmins(view);
        check(v.ok && view.number == 1 && view.admins.size() == 1 && view.admins[0].admin.name == kLea && view.editors.size() == 1,
              "un admin lit la liste (en lecture seule) (" + v.text() + ")");
        const auto t = leaWs.writeAdminToken(true);
        std::vector<lic::SignedBlock> blocks;
        adm::AdminToken token;
        check(t.ok && lic::readBlocks(readFile(root / "lea2" / "jeton-admin.xpgjet"), blocks, nullptr) && blocks.size() == 1 &&
                  lic::verifyBlock(blocks[0], leaWs.signer()->publicKey()) && adm::parseAdminToken(blocks[0].document, token, nullptr) && token.name == kLea,
              "son jeton d'admin : signe par sa cle, a son nom");
    }

    const fs::path clientDir = root / "pc-client";
    fs::create_directories(clientDir, ec);
    lic::LicenseStore client(clientOptions(clientDir, *plus.signingKey()));
    std::string leaLicenceHash;
    std::printf("-- une licence signee par l'admin, installee chez un client\n");
    {
        std::string why;
        const std::string req = U(root / "client.xpgreq");
        check(client.writeActivationRequest(req, "PC client essai", "", "IHM Studio 1.44.0 (essai)", &why), "la demande du client (" + why + ")");
        gui::LicenceDraft d;
        d.licenseId = "XPG-ESSAI-LEA-1";
        d.customer = kCustomer;
        d.edition = "PROFESSIONAL";
        d.requestPaths = {req};
        const std::string out = U(root / "sortie" / "lea-1.xpglic");
        const auto r = leaWs.signLicence(d, out);
        check(r.ok, "Lea signe la licence (" + r.text() + ")");
        const std::string file = readFile(out);
        std::vector<lic::SignedBlock> blocks;
        check(lic::readBlocks(file, blocks, nullptr) && blocks.size() >= 2 && blocks[0].kind == lic::DocKind::Admins && blocks[1].kind == lic::DocKind::License &&
                  blocks[1].keyId == leaWs.adminKey().id,
              "le fichier : la liste des admins d'abord, puis la licence signee par Lea");
        if (blocks.size() >= 2) leaLicenceHash = adm::documentHash(blocks[1].document);
        const auto rep = client.importBytes(file);
        check(rep.ok && rep.licenseInstalled && rep.state.status == lic::LicenseStatus::Valid, "le client l'installe : valide (" + rep.why + ")");
        auto job = leaWs.takeServiceJob();
        const auto pub = job ? leaWs.serviceRun(*job) : gui::OpResult::fail("pas de travail");
        check(pub.ok, "publiee a son PC par le service, signee par Lea (" + pub.text() + ")");
    }

    std::printf("-- un droit qui manque : les offres\n");
    {
        lic::Registry edited = leaWs.registry();
        edited.editions.front().price = "2 \xE2\x82\xAC HT (essai)";
        const auto r = leaWs.signRegistry(edited);
        check(!r.ok && has(r.message, "Modifier les offres") && has(r.message, "admin+"), "refuse ici : " + r.message);
        // force : signe a la main par sa cle, depose quand meme -> 403 droit
        std::string why, out;
        auto key = la::loadAdminKey(U(root / "lea2"), &why);
        lic::Registry reg = edited;
        reg.number = leaWs.registry().number + 1;
        check(key && la::signRegistry(*key, lic::writeRegistry(reg), lic::systemNow(), out, &why), "signe a la main (" + why + ")");
        json::Value body = json::Value::object();
        body.set("registre", out);
        const Reply rep = key ? call(svc.url(), "PUT", "/admin/registre", json::write(body, false), key.get()) : Reply{};
        check(rep.status == 403 && rep.value.str("erreur") == "droit", "et le service refuse : 403 droit (" + std::to_string(rep.status) + " " + rep.body + ")");
        // une option : son droit
        lic::Registry o = leaWs.registry();
        if (!o.options.empty()) {
            o.options.front().label += " (essai)";
            const auto s = leaWs.signRegistry(o);
            check(s.ok, "une option changee : son droit, signee par Lea (" + s.text() + ")");
            auto job = leaWs.takeServiceJob();
            const auto pub = job ? leaWs.serviceRun(*job) : gui::OpResult::fail("pas de travail");
            check(pub.ok, "le service la prend (le droit << options >>) (" + pub.text() + ")");
        }
    }

    std::printf("-- l'archive entre l'admin et l'admin+ (documents d'un admin, reglages partages)\n");
    {
        const auto s1 = leaWs.archiveSync(false, false);
        check(s1.ok, "Lea envoie sa licence et le registre (" + s1.text() + ")");
        const auto s2 = plus.archiveSync(false, false);
        plus.reload();
        check(s2.ok && plus.licence("XPG-ESSAI-LEA-1") != nullptr, "l'admin+ recoit la licence signee par Lea (" + s2.text() + ")");
        check(plus.registry().number == leaWs.registry().number, "et le registre signe par Lea (son droit)");
        check(has(s2.text(), "profils-admins.json"), "les profils de l'admin+ partent dans l'archive (scelles)");
        const auto s3 = leaWs.archiveSync(true, false);
        bool lv2 = false;
        for (const auto& p : leaWs.profiles()) lv2 = lv2 || p.name == "Admin LV2";
        check(s3.ok && fs::exists(root / "lea2" / ar::kProfilesFile) && lv2, "Lea les recoit (" + s3.text() + ")");
    }

    std::printf("-- le journal des admins\n");
    {
        const auto f = leaWs.flushJournal();
        check(f.ok && leaWs.journalWaiting() == 0, "le journal de Lea envoye (" + f.text() + ")");
        int added = 0;
        const auto r = plus.fetchJournal(&added);
        check(r.ok && added >= 5, "l'admin+ releve le journal (" + r.text() + ")");
        const auto v = plus.journalLines();
        check(v.problems.empty(), "toutes les lignes verifiees, la chaine entiere" + problemsOf(v));
        const auto* creer = rowOf(v, "licence_creer", kLea);
        const auto* ajout = rowOf(v, "admin_ajouter", "admin+");
        const auto* demande = rowOf(v, "demande_admin", "le service");
        check(creer && creer->verified && creer->line.parsed.object == "XPG-ESSAI-LEA-1" && creer->line.key == leaWs.adminKey().keyHex,
              "la licence de Lea : sa ligne, signee par sa cle");
        check(ajout && ajout->verified && ajout->level == adm::Level::Critical && ajout->notify, "l'ajout de l'admin : par l'admin+, critique");
        check(demande && demande->verified, "la demande d'admin : ecrite par le service (sa cle certifiee)");
        check(rowOf(v, "options", kLea) != nullptr, "l'option changee par Lea");
        const auto alerts = plus.newAlerts();
        check(!alerts.empty() && plus.newAlerts().empty(), "les alertes : une fois (" + std::to_string(alerts.size()) + ")");
        check(!leaWs.fetchJournal(nullptr).ok, "un admin ne releve pas le journal (l'admin+ seulement)");
        // une copie modifiee se voit
        const fs::path path = root / "plus" / "journal-admins.jsonl";
        const std::string good = readFile(path);
        std::string bad;
        std::size_t pos = 0;
        bool changed = false;
        while (pos < good.size()) {
            std::size_t end = good.find('\n', pos);
            if (end == std::string::npos) end = good.size();
            std::string line = good.substr(pos, end - pos);
            if (!changed && has(line, "licence_creer") && has(line, "XPG-ESSAI-LEA-1")) {
                const auto at = line.find("XPG-ESSAI-LEA-1");
                line.replace(at, 15, "XPG-ESSAI-LEA-9");
                changed = true;
            }
            bad += line + "\n";
            pos = end + 1;
        }
        writeFile(path, bad);
        const auto t = plus.journalLines();
        check(changed && !t.problems.empty() && (has(problemsOf(t), "signature fausse") || has(problemsOf(t), "cha\xC3\xAEne rompue")),
              "une ligne modifiee dans la copie de ce poste : vue" + problemsOf(t));
        // une ligne enlevee
        const auto first = good.find('\n');
        const auto second = good.find('\n', first + 1);
        writeFile(path, good.substr(0, first + 1) + good.substr(second + 1));
        const auto u = plus.journalLines();
        check(has(problemsOf(u), "cha\xC3\xAEne rompue"), "une ligne enlevee : la chaine rompue" + problemsOf(u));
        writeFile(path, good);
        check(plus.journalLines().problems.empty(), "la copie remise : plus rien a dire");
        // la criticite
        check(plus.setCriticality("licence_creer", adm::Level::High, true).ok && plus.criticality().actions.at("licence_creer").level == adm::Level::High,
              "la criticite d'un type d'action");
        const auto* c2 = rowOf(plus.journalLines(), "licence_creer", kLea);
        check(c2 && c2->level == adm::Level::High && c2->notify, "la ligne suit son type");
        const std::uint64_t n = c2 ? c2->line.number : 0;
        check(plus.setLineLevel(n, adm::Level::Critical).ok, "une ligne changee a la main");
        const auto* c3 = rowOf(plus.journalLines(), "licence_creer", kLea);
        check(c3 && c3->overridden && c3->level == adm::Level::Critical && !c3->overrideBy.empty(), "sa criticite, qui et quand");
        check(plus.setEmailCritical(true).ok && plus.criticality().emailCritical, "aussi par courriel");
        check(!leaWs.setCriticality("licence_creer", adm::Level::Info, false).ok, "la criticite : l'admin+ seulement");
    }

    std::printf("-- les profils\n");
    {
        adm::Profile p;
        p.name = "Admin LV1";
        p.rights = {"demandes", "codes"};
        check(plus.saveProfile(p).ok, "un profil cree");
        check(plus.renameProfile("Admin LV1", "Admin niveau 1").ok, "renomme");
        bool oldName = false, newName = false;
        for (const auto& q : plus.profiles()) {
            oldName = oldName || q.name == "Admin LV1";
            newName = newName || (q.name == "Admin niveau 1" && q.rights.size() == 2);
        }
        check(!oldName && newName, "relu : le nouveau nom, ses droits");
        check(!plus.renameProfile(adm::kAllRightsProfile, "X").ok && !plus.deleteProfile(adm::kAllRightsProfile).ok, "<< Tous les droits >> reste");
        check(plus.deleteProfile("Admin niveau 1").ok && plus.profiles().size() == 2, "supprime (restent : Tous les droits, Admin LV2)");
    }

    std::printf("-- un droit retire : ce qu'il a signe reste valable\n");
    {
        const auto r = plus.copyProfileTo("Admin LV2", {leaWs.adminKey().keyHex});
        check(r.ok && has(r.text(), "ne changent pas") == false, "copier un profil vers l'admin (" + r.text() + ")");
        const auto s = plus.setAdminRights(leaWs.adminKey().keyHex, {"demandes", "codes", "options"}, "Admin LV2 ajust\xC3\xA9");
        plus.reload();
        const adm::Admin* l = plus.adminList() ? plus.adminList()->byKey(leaWs.adminKey().keyHex) : nullptr;
        check(s.ok && l && l->rights.size() == 3 && !leaLicenceHash.empty() &&
                  std::find(l->kept.begin(), l->kept.end(), leaLicenceHash) != l->kept.end(),
              "ses droits de licence retires : sa licence en vigueur est gardee (" + s.text() + ")");
        const Reply list = call(svc.url(), "GET", "/v1/admins", {}, nullptr);
        bool changed = false;
        std::string why;
        check(list.status == 200 && client.adoptAdminList(list.body, &changed, &why) && changed, "le client prend la nouvelle liste (" + why + ")");
        check(client.evaluate().status == lic::LicenseStatus::Valid, "sa licence reste valide chez le client");
        (void)leaWs.refreshAdminState(nullptr, nullptr);
        leaWs.reload();
        check(leaWs.myRights().size() == 3 && !leaWs.can("licence_creer"), "Lea voit ses droits changes (le regard)");
    }

    std::printf("-- retirer l'admin\n");
    {
        const auto before = plus.registry().number;
        const auto r = plus.retireAdmin(leaWs.adminKey().keyHex, false);
        check(r.ok, "retire (" + r.text() + ")");
        plus.reload();
        const adm::Admin* l = plus.adminList() ? plus.adminList()->byKey(leaWs.adminKey().keyHex) : nullptr;
        check(l && !l->active() && l->rights.empty() && std::find(l->kept.begin(), l->kept.end(), leaLicenceHash) != l->kept.end(),
              "dans la liste : retire, sans droit, sa licence gardee");
        std::vector<lic::SignedBlock> blocks;
        const std::string regText = ar::archivedText(U(root / "plus"), "registre/registre.xpgreg");
        check(lic::readBlocks(regText, blocks, nullptr) && blocks.size() == 1 && blocks[0].keyId == plus.signingKey()->keyId() &&
                  plus.registry().number == before + 1,
              "le registre qu'il avait signe : re-signe par la cle de l'editeur (n. " + std::to_string(plus.registry().number) + ")");
        const Reply etat = call(svc.url(), "GET", "/v1/etat", {}, nullptr);
        check(static_cast<std::uint64_t>(etat.value.integer("registre")) == plus.registry().number, "et publie");
        // le client : la liste du retrait ; la licence gardee reste valide
        const Reply list = call(svc.url(), "GET", "/v1/admins", {}, nullptr);
        bool changed = false;
        std::string why;
        check(list.status == 200 && client.adoptAdminList(list.body, &changed, &why) && changed && client.evaluate().status == lic::LicenseStatus::Valid,
              "chez le client : la liste du retrait, sa licence toujours valide (" + why + ")");
        // l'admin retire
        gui::AdminRefresh x;
        (void)leaWs.refreshAdminState(nullptr, &x);
        leaWs.reload();
        check(leaWs.role() == gui::Role::Retired && !leaWs.signer() && has(leaWs.whyNot("demandes"), "retir"), "Lea se voit retiree : rien ne signe");
        gui::LicenceDraft d;
        d.licenseId = "XPG-ESSAI-LEA-2";
        d.customer = kCustomer;
        d.edition = "PROFESSIONAL";
        d.requestPaths = {U(root / "client.xpgreq")};
        check(!leaWs.signLicence(d, U(root / "sortie" / "lea-2.xpglic")).ok, "son outil refuse de signer");
        on::Snapshot snap;
        const auto sc = leaWs.serviceListLicences(snap);
        check(!sc.ok && has(sc.message, "retir"), "le service lui repond 403 retire (" + sc.message + ")");
        // une licence qu'il signerait quand meme (sa cle, a la main) : refusee par le client
        auto key = la::loadAdminKey(U(root / "lea2"), &why);
        check(client.writeActivationRequest(U(root / "client2.xpgreq"), "PC client essai", "", "IHM Studio 1.44.0 (essai)", &why), "une autre demande du client");
        la::LicenseInputs in;
        json::Value p = json::Value::object();
        p.set("license_id", "XPG-ESSAI-LEA-2");
        p.set("customer", kCustomer);
        p.set("edition", "PROFESSIONAL");
        in.params = json::write(p);
        in.registry = ar::archivedText(U(root / "lea2"), "registre/registre.xpgreg");
        in.requests = {readFile(root / "client2.xpgreq")};
        const auto* editor = plus.signingKey();
        in.alsoTrusted = [editor](const lic::SignedBlock& b) { return lic::verifyBlock(b, editor->publicKey()); };
        std::string out, summary;
        const bool signedByHand = key && la::signLicense(*key, in, lic::systemNow(), out, &why, &summary);
        check(signedByHand, "signee a la main par sa cle (" + why + ")");
        const auto rep = client.importBytes(list.body + out);
        check(!rep.ok && !rep.licenseInstalled, "le client la refuse : un admin retire (" + rep.why + ")");
        check(client.evaluate().status == lic::LicenseStatus::Valid && client.evaluate().licenseId == "XPG-ESSAI-LEA-1", "la licence gardee reste installee");
    }

    std::printf("-- un second admin, retire avec ses licences revoquees\n");
    {
        gui::Workspace hugo(U(root / "hugo"));
        check(hugo.serviceConfigure(svc.url()).ok, "le poste de Hugo");
        check(hugo.requestAdmin("Hugo Essai", "hugo@exemple.invalid", nullptr).ok, "sa demande");
        hugo.reload();
        const std::vector<std::string> rights{"licence_creer"};
        const auto a = plus.acceptAdmin(hugo.adminKey().keyHex, "", &rights);
        check(a.ok, "accepte avec des droits choisis (sans profil) (" + a.text() + ")");
        (void)hugo.refreshAdminState(nullptr, nullptr);
        hugo.reload();
        check(hugo.role() == gui::Role::Admin && hugo.myRights() == rights && hugo.archiveSync(true, false).ok, "Hugo : admin, l'archive recue");
        hugo.reload();
        lic::LicenseStore client2(clientOptions(root / "pc-client-2", *plus.signingKey()));
        std::string why;
        check(client2.writeActivationRequest(U(root / "client-2.xpgreq"), "PC client essai 2", "", "IHM Studio 1.44.0 (essai)", &why), "la demande du second client");
        gui::LicenceDraft d;
        d.licenseId = "XPG-ESSAI-HUGO-1";
        d.customer = "Atelier d'essai deux";
        d.edition = "PROFESSIONAL";
        d.requestPaths = {U(root / "client-2.xpgreq")};
        const auto r = hugo.signLicence(d, U(root / "sortie" / "hugo-1.xpglic"));
        auto job = hugo.takeServiceJob();
        check(r.ok && job && hugo.serviceRun(*job).ok, "Hugo signe et publie une licence (" + r.text() + ")");
        const auto rep = client2.importBytes(readFile(root / "sortie" / "hugo-1.xpglic"));
        check(rep.ok && rep.state.status == lic::LicenseStatus::Valid, "le second client l'installe (" + rep.why + ")");
        check(hugo.archiveSync(false, false).ok && plus.archiveSync(false, false).ok, "l'archive : sa licence chez l'admin+");
        plus.reload();
        const auto ret = plus.retireAdmin(hugo.adminKey().keyHex, true);
        check(ret.ok && has(ret.text(), "r\xC3\xA9voqu"), "retire, ses licences revoquees (" + ret.text() + ")");
        plus.reload();
        const gui::LicenceEntry* e = plus.licence("XPG-ESSAI-HUGO-1");
        std::vector<lic::SignedBlock> blocks;
        check(e && e->revoked && lic::readBlocks(ar::archivedText(U(root / "plus"), "revocations/revocations.xpgrev"), blocks, nullptr) && blocks.size() == 1 &&
                  blocks[0].keyId == plus.signingKey()->keyId(),
              "la licence de Hugo revoquee par une liste signee de la cle de l'editeur");
        const adm::Admin* h = plus.adminList() ? plus.adminList()->byKey(hugo.adminKey().keyHex) : nullptr;
        check(h && !h->active() && h->kept.empty(), "Hugo retire, rien de garde");
        // son PC prevenu : l'etat << revoquee >> (signe par la cle de l'editeur)
        auto pcKey = lic::SigningKey::load(U(root / "pc-client-2" / lic::kDeviceKeyFile), lic::deviceKeySpec(), &why);
        const Reply poste = pcKey ? call(svc.url(), "GET", "/v1/poste", {}, pcKey.get()) : Reply{};
        std::vector<lic::SignedBlock> pb;
        lic::gateway::DeviceStatus st;
        check(poste.status == 200 && poste.value.isObject() && lic::readBlocks(poste.value.str("poste"), pb, nullptr) && pb.size() == 1 &&
                  lic::verifyBlock(pb[0], plus.signingKey()->publicKey()) && lic::gateway::parseDeviceStatus(pb[0].document, st, nullptr) &&
                  st.state == lic::gateway::State::Revoked,
              "son PC : << revoquee >>, signe par l'editeur (" + std::to_string(poste.status) + " " + (pb.empty() ? poste.body : pb[0].document) + " ; " + ret.text() + ")");
        const Reply list = call(svc.url(), "GET", "/v1/admins", {}, nullptr);
        bool changed = false;
        check(client2.adoptAdminList(list.body, &changed, &why) && client2.evaluate().status != lic::LicenseStatus::Valid,
              "chez le client, avec la nouvelle liste : la licence n'est plus valable");
    }

    std::printf("-- le journal apres le retrait\n");
    {
        int added = 0;
        const auto r = plus.fetchJournal(&added);
        const auto v = plus.journalLines();
        check(r.ok && v.problems.empty() && rowOf(v, "admin_retirer", "admin+") && rowOf(v, "admin_droits", "admin+") && rowOf(v, "signature_refusee", "le service"),
              "le retrait, les droits, la signature refusee par le service : au journal, verifies" + problemsOf(v));
    }

    return finish();
}
