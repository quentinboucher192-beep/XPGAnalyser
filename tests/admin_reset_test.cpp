// =============================================================================
//  tests/admin_reset_test.cpp - 1.49.0 : REPARTIR DE ZERO (docs/ADMINS.md,
//  par. 13 ; gui/WorkspaceReset.cpp, gui/ResetDialog.cpp)
// -----------------------------------------------------------------------------
//  Sans reseau : le bandeau du premier lancement de l'Admin+ 1.49 (montre UNE
//  fois, puis plus jamais ; << Non merci >> le ferme), la carte de Service en
//  ligne, la fenetre (le mot REPARTIR, la sauvegarde pas reglee : gris) ; XPG
//  Admin (l'edition classique) : jamais rien de tout cela.
//  Contre le VRAI Worker sur ce poste (tests/service_local.hpp) : un admin+ bien
//  rempli (une licence en ligne et son code, une licence et sa demande, un admin
//  accepte, le journal releve, l'archive synchronisee, les petits fichiers d'aide),
//  les refus (le mot, la sauvegarde pas reglee : rien n'est efface), puis la
//  fenetre : REPARTIR tape, << Repartir de zero >> - la sauvegarde faite, le
//  service vide (les licences, l'admin), le journal reparti a la ligne 1, le
//  dossier de ce poste vide de ce qui est efface et intact pour ce qui est garde ;
//  enfin la sauvegarde restauree remet tout. SAUTE sans Node.js ni wrangler.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/Edition.hpp"
#include "../src/license_admin/KeyBackup.hpp"
#include "../src/license_admin/ServiceClient.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Time.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace admintest;
namespace la = license_admin;

namespace {

const std::string kPhrase = "une phrase de passe d'essai 2026 pour repartir";

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string draw(ui::Widget& w, float width = 1400.f, float height = 3200.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
std::string drawMenu(menu::IMenu& m, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paintMenu(m, r, width, height);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool enabledOf(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->enabled();
}
bool present(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
// Un appel au service signe par la cle de l'editeur (les verifications de l'essai).
namespace online = license_admin::online;
online::Reply ask(const std::string& url, const lic::SigningKey& key, const std::string& method, const std::string& path, bool signed_ = true) {
    online::Options o;
    o.now = lic::systemNow();
    o.tool = "admin_reset_test";
    online::Client c(url, key, o);
    return c.call(method, path, {}, signed_);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_reset_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    // ======================================================================== sans reseau
    std::printf("-- le bandeau du premier lancement (une fois), la carte, la fenetre ; XPG Admin : jamais\n");
    {
        Fixture fx(source, "repartir-bandeau");
        check(fx.ready(), "l'admin+ (cle d'essai, registre)");
        check(fx.licence("XPG-2026-RZ001", "Atelier Exemple", "PROFESSIONAL", {fx.request("pc1", "PC-ATELIER")}).ok, "une licence et son PC");
        fx.ws->reload();
        check(fx.ws->startOverOfferDue(), "la proposition est due (l'admin+ 1.49, quelque chose a effacer, jamais proposee)");
        {
            Fixture empty(source, "repartir-vide");
            check(empty.ready() && !empty.ws->startOverOfferDue(), "un admin+ neuf (rien a effacer) : pas de bandeau");
        }
        HostRig rig(fx);
        auto dash = gui::makeDashboardPage(rig.host());
        dash->refresh();
        std::string t = draw(*dash, 1300.f, 900.f);
        check(shown(*dash, "tableau.repartir") && shown(*dash, "tableau.repartir.fermer") && has(t, "La 1.49 peut repartir de z\xC3\xA9ro"),
              "le tableau de bord : le bandeau << La 1.49 peut repartir de zero >>, Voir... et Non merci");
        check(present(fx.admin / "repartir-de-zero.json") && !fx.ws->startOverOfferDue(), "montre : note (repartir-de-zero.json), plus du");
        check(click(*dash, "tableau.repartir") && rig.dialogs.size() == 1, "Voir... : la fenetre Repartir de zero");
        if (!rig.dialogs.empty() && rig.dialogs.back()) {
            auto& dlg = *rig.dialogs.back();
            const std::string d = drawMenu(dlg);
            ui::Widget& root = *dlg.widgetRoot();
            check(has(d, "Effac\xC3\xA9 (le service et ce poste)") && has(d, "Gard\xC3\xA9") && has(d, "Les licences et leurs fichiers") &&
                      has(d, "Les admins : une nouvelle liste sign\xC3\xA9" "e sans aucun admin") && has(d, "La cl\xC3\xA9 de l'\xC3\xA9" "diteur et ton identit\xC3\xA9"),
                  "ce qui est efface, ce qui est garde");
            check(has(d, "perdent leur licence \xC3\xA0 leur prochain contr\xC3\xB4le"), "les Studios deja actives : dit clairement");
            check(has(d, "La sauvegarde compl\xC3\xA8te n'est pas r\xC3\xA9gl\xC3\xA9" "e"), "la sauvegarde pas reglee : dit");
            check(type(root, "repartir.mot", "REPARTIR"), "REPARTIR tape");
            (void)drawMenu(dlg);
            check(!enabledOf(root, "repartir.ok"), "sans sauvegarde reglee : << Repartir de zero >> reste gris");
        }
        check(click(*dash, "tableau.repartir.fermer"), "Non merci");
        (void)draw(*dash, 1300.f, 900.f);
        check(!shown(*dash, "tableau.repartir"), "le bandeau ferme");
        // le lancement suivant (un Workspace neuf, une page neuve) : plus jamais
        {
            HostRig rig2(fx);
            fx.ws = std::make_unique<gui::Workspace>(U(fx.admin));
            fx.ws->setClock([] { return g_now; });
            fx.ws->reload();
            auto dash2 = gui::makeDashboardPage(rig2.host());
            dash2->refresh();
            const std::string t2 = draw(*dash2, 1300.f, 900.f);
            check(!shown(*dash2, "tableau.repartir") && !has(t2, "La 1.49 peut repartir de z\xC3\xA9ro"), "le lancement suivant : le bandeau ne revient pas");
        }
        HostRig rig3(fx);
        auto sp = gui::makeServicePage(rig3.host());
        sp->refresh();
        t = draw(*sp);
        check(shown(*sp, "service.repartir") && has(t, "Repartir de z\xC3\xA9ro"), "Service en ligne : la carte Repartir de zero (l'admin+)");
        // XPG Admin, l'edition classique : jamais
        la::setEdition(la::Edition::Classique);
        {
            check(!fx.ws->startOverOfferDue() && !fx.ws->whyNotStartOver().empty(), "XPG Admin : pas de proposition, et il dit pourquoi");
            check(!gui::makeStartOverDialog(rig3.host()), "XPG Admin : la fenetre ne s'ouvre pas");
            check(!fx.ws->startOver("REPARTIR").ok, "XPG Admin : startOver refuse");
            auto sp2 = gui::makeServicePage(rig3.host());
            sp2->refresh();
            const std::string t2 = draw(*sp2);
            check(!shown(*sp2, "service.repartir") && !has(t2, "Repartir de z\xC3\xA9ro"), "XPG Admin : pas de carte Repartir de zero");
            Fixture other(source, "repartir-classique");
            check(other.ready(), "un autre dossier (cle d'essai)");
            HostRig rig4(other);
            auto dash4 = gui::makeDashboardPage(rig4.host());
            dash4->refresh();
            check(!shown(*dash4, "tableau.repartir") && !present(other.admin / "repartir-de-zero.json"), "XPG Admin : jamais le bandeau");
        }
        la::setEdition(la::Edition::Plus);
    }

    // ======================================================================== le service local
    Fixture fx(source, "repartir");
    fx.ws->setClock([] { return lic::systemNow(); });
    check(fx.ready(), "l'admin+ (cle d'essai, registre)");
    gui::Workspace& plus = *fx.ws;
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
    const fs::path A = fx.admin;

    std::printf("-- un admin+ bien rempli\n");
    int adminsBefore = 0;
    std::uint64_t revBefore = 0, regBefore = 0;
    std::string registryBefore;
    {
        check(plus.serviceConfigure(svc.url()).ok, "l'adresse du service");
        check(plus.serviceCertify({}).ok, "la cle du service certifiee");
        std::string code;
        check(plus.syncKeyCreate(&code).ok, "la cle de synchro");
        auto r = plus.signRegistry(plus.registry());
        auto job = plus.takeServiceJob();
        check(r.ok && job && plus.serviceRun(*job).ok, "le registre depose");
        plus.reload();
        gui::LicenceDraft d;
        d.online = true;
        d.licenseId = "XPG-2026-RZ101";
        d.customer = "Soci\xC3\xA9t\xC3\xA9 d'essai";
        d.edition = "PERSONAL";
        d.maxDevices = 2;
        d.durationDays = 90;
        std::string onlineCode;
        gui::ServiceJob oj;
        r = plus.prepareOnlineLicence(d, {}, oj, onlineCode);
        check(r.ok && plus.serviceRunOnline(oj, onlineCode).ok, "une licence en ligne deposee, son code garde (" + r.text() + ")");
        check(fx.licence("XPG-2026-RZ102", "Atelier Exemple", "PROFESSIONAL", {fx.request("pc2", "PC-ATELIER-2")}).ok, "une licence et sa demande (activations.jsonl)");
        gui::Workspace lea(U(fx.root / "lea"));
        check(lea.serviceConfigure(svc.url()).ok, "le poste de Lea : l'adresse");
        std::string fp;
        r = lea.requestAdmin("L\xC3\xA9" "a Essai", "lea@exemple.invalid", &fp);
        check(r.ok, "Lea demande a devenir admin (" + r.text() + ")");
        r = plus.acceptAdmin(lea.adminKey().keyHex, "Tous les droits");
        check(r.ok, "acceptee (" + r.text() + ")");
        plus.reload();
        check(plus.fetchJournal().ok && !plus.journalLines().rows.empty(), "le journal releve (journal-admins.jsonl)");
        check(plus.archiveSync(false, false).ok, "l'archive synchronisee");
        // les petits fichiers d'aide (effaces) et un reglage d'ici (garde)
        writeFile(A / "invitations-admins.json", "{\"format\":\"xpg-invitations-admins/1\",\"invitations\":[{\"nom\":\"Nina Essai\"}]}\n");
        writeFile(A / "pc-supprimes.json", "{}\n");
        writeFile(A / "stats-a-envoyer.json", "{}\n");
        writeFile(A / "clients-supprimes.json", "{}\n");
        writeFile(A / "statistiques-tableau.json", "{}\n");
        writeFile(A / "corbeille-licences" / "XPG-2026-RZ000.xpglic", "ancienne");
        writeFile(A / "listes-filtres.txt", "un filtre d'essai\n");
        plus.reload();
        check(plus.adminList() && plus.adminList()->admins.size() == 1, "un admin dans la liste");
        adminsBefore = plus.adminList() ? plus.adminList()->number : 0;
        revBefore = plus.hasRevocations() ? plus.revocations().number : 0;
        regBefore = plus.registry().number;
        registryBefore = readFile(A / "archive" / "registre" / "registre.xpgreg");
        check(plus.licences().size() == 2 && present(A / "activations.jsonl") && present(A / "codes-activation") && present(A / "journal-admins.jsonl"),
              "deux licences, le registre des activations, un code, le journal");
        const auto l = ask(svc.url(), editor, "GET", "/admin/licences");
        check(l.status == 200 && has(l.body, "XPG-2026-RZ101"), "le service a la licence en ligne");
    }

    std::printf("-- les refus : rien n'est efface\n");
    {
        auto r = plus.startOver("repartir");
        check(!r.ok && has(r.message, "REPARTIR"), "sans le mot exact : refuse (" + r.message + ")");
        check(!plus.whyNotStartOver().empty() && has(plus.whyNotStartOver(), "sauvegarde"), "la sauvegarde pas reglee : le bouton dit pourquoi");
        r = plus.startOver("REPARTIR");
        check(!r.ok && has(r.message, "sauvegarde"), "sans sauvegarde reglee : refuse (" + r.message + ")");
        plus.reload();
        check(plus.licences().size() == 2 && has(ask(svc.url(), editor, "GET", "/admin/licences").body, "XPG-2026-RZ101"), "rien n'est efface (ici et sur le service)");
        la::KeyBackupOptions weak;   // les essais : Argon2id au plus bas permis
        weak.opsLimit = 2;
        weak.memLimit = 64u * 1024u * 1024u;
        r = plus.configureBackup(U(fx.root / "sauvegardes"), kPhrase, weak);
        check(r.ok && plus.whyNotStartOver().empty(), "la sauvegarde reglee : permis (" + r.text() + ")");
    }

    std::printf("-- la fenetre : REPARTIR, Repartir de zero\n");
    std::string backupPath;
    {
        HostRig rig(fx);
        auto sp = gui::makeServicePage(rig.host());
        sp->refresh();
        (void)draw(*sp);
        check(enabledOf(*sp, "service.repartir") && click(*sp, "service.repartir") && rig.dialogs.size() == 1, "Service en ligne : Repartir de zero... ouvre la fenetre");
        if (!rig.dialogs.empty() && rig.dialogs.back()) {
            auto& dlg = *rig.dialogs.back();
            ui::Widget& root = *dlg.widgetRoot();
            std::string d = drawMenu(dlg);
            check(has(d, "D'abord, une sauvegarde compl\xC3\xA8te exacte") && has(d, U(fx.root / "sauvegardes")) && has(d, "Restaurer une sauvegarde"),
                  "la fenetre dit ou va la sauvegarde, et que Restaurer remet tout");
            check(!enabledOf(root, "repartir.ok"), "sans le mot : gris");
            check(type(root, "repartir.mot", "repartir"), "un mot faux");
            (void)drawMenu(dlg);
            check(!enabledOf(root, "repartir.ok"), "en minuscules : gris");
            check(type(root, "repartir.mot", "REPARTIR"), "REPARTIR");
            (void)drawMenu(dlg);
            check(enabledOf(root, "repartir.ok"), "REPARTIR tape : << Repartir de zero >> permis");
            check(click(root, "repartir.ok"), "Repartir de zero");
        }
        check(!rig.reports.empty() && rig.reports.back().ok, "fait : " + (rig.reports.empty() ? std::string("(rien)") : rig.reports.back().text()));
        if (!rig.reports.empty()) std::printf("%s\n", rig.reports.back().text().c_str());
        backupPath = plus.backupSettings().lastPath;
    }

    std::printf("-- ce poste : efface, garde\n");
    {
        plus.reload();
        check(!backupPath.empty() && present(backupPath), "la sauvegarde complete d'avant : " + backupPath);
        for (const char* f : {"activations.jsonl", "invitations-admins.json", "pc-supprimes.json", "stats-a-envoyer.json", "clients-supprimes.json",
                              "statistiques-tableau.json", "journal-admins-etat.json", "journal-a-envoyer.jsonl", "service-releve.json"})
            check(!present(A / f), std::string("efface : ") + f);
        for (const char* d : {"archive/licences", "corbeille-licences", "codes-activation"}) check(!present(A / d), std::string("efface : ") + d + "/");
        for (const char* f : {la::kPublisherKeyFile, "service.json", "sauvegarde.json", "sauvegarde.cle", "cle-synchro.cle", "listes-filtres.txt",
                              "archive/registre/registre.xpgreg"})
            check(present(A / f), std::string("garde : ") + f);
        check(readFile(A / "archive" / "registre" / "registre.xpgreg") == registryBefore && plus.registry().number == regBefore,
              "le registre (le catalogue) : intact, le meme numero");
        check(plus.role() == gui::Role::Plus && plus.licences().empty() && plus.clients().empty(), "toujours l'admin+ ; plus aucune licence, aucun client");
        check(plus.adminList() && plus.adminList()->admins.empty() && plus.adminList()->number == adminsBefore + 1, "la liste des admins : vide, numero + 1");
        check(plus.hasRevocations() && plus.revocations().revoked.empty() && plus.revocations().number == revBefore + 1, "la liste de revocation : vide, numero + 1");
        check(!plus.startOverOfferDue(), "fait : plus de proposition");
    }

    std::printf("-- le service : vide, le journal reparti\n");
    {
        const auto l = ask(svc.url(), editor, "GET", "/admin/licences");
        check(l.status == 200 && !has(l.body, "XPG-2026-RZ101"), "le service n'a plus de licence");
        const auto a = ask(svc.url(), editor, "GET", "/v1/admins", false);
        check(a.status == 200 && has(a.body, "\"admins\":[]"), "la liste publique des admins : vide");
        check(ask(svc.url(), editor, "GET", "/v1/certificat", false).status == 200, "le certificat du service : garde");
        check(plus.fetchJournal().ok, "le journal releve de nouveau");
        const gui::JournalView v = plus.journalLines();
        check(!v.rows.empty() && v.rows.front().line.number == 1 && v.rows.front().line.previous.empty() && has(v.rows.front().line.entry, "\"reinitialisation\"") &&
                  has(v.rows.front().line.entry, "r\xC3\xA9initialisation par l'admin+"),
              "la ligne n 1 : << reinitialisation par l'admin+ >>, precedent vide");
        check(v.problems.empty(), "la chaine repart proprement (aucune rupture)" + (v.problems.empty() ? std::string() : " : " + v.problems.front()));
    }

    std::printf("-- la sauvegarde d'avant remet tout\n");
    {
        gui::RestoreOptions o;
        o.restoreService = true;
        o.replaceService = true;
        gui::RestoreReport rep;
        const auto r = plus.restoreBackup(backupPath, kPhrase, o, &rep);
        check(r.ok && rep.serviceRestored, "restauree : le dossier et le service (" + r.text() + ")");
        plus.reload();
        check(plus.licences().size() == 2 && plus.adminList() && plus.adminList()->admins.size() == 1, "les licences et l'admin reviennent sur le poste");
        check(has(ask(svc.url(), editor, "GET", "/admin/licences").body, "XPG-2026-RZ101"), "la licence en ligne revient sur le service");
    }
    return finish();
}
