// =============================================================================
//  tests/admin_workspace_test.cpp - 1.33.0 : le poste Admin vu par l'application
//  (license_admin/gui/Workspace), sans ecran
// -----------------------------------------------------------------------------
//  Un poste d'essai (admin_test_fixture.hpp : une cle d'ESSAI, le registre
//  d'exemple, des PC fictifs) : la cle, les demandes, signer une licence,
//  l'etendre, revoquer un PC puis la licence, retirer une revocation, le
//  registre modifie, ranger des fichiers, le tableau de bord, les clients, la
//  sauvegarde de la cle, la configuration GitHub (sans reseau), le travail lent.
// =============================================================================
#include "admin_test_fixture.hpp"

#include <thread>

using namespace admintest;

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    if (!deploy::crypto::init()) return 1;

    std::printf("-- un poste neuf\n");
    Fixture f(source, "ws");
    auto& ws = *f.ws;
    check(!ws.key().present && !ws.key().problem.empty() && !ws.hasRegistry() && ws.licences().empty() && ws.records().empty(),
          "rien : ni cle, ni registre, ni licence (" + ws.key().problem + ")");
    {
        const auto d = ws.dashboard();
        check(d.licences == 0 && d.clients == 0 && d.devices == 0 && d.recent.empty(), "le tableau de bord d'un poste neuf : zero partout");
    }
    check(!ws.preview(gui::LicenceDraft{}).ok, "sans cle : pas d'apercu de licence");

    std::printf("-- la cle et le registre\n");
    check(f.ready(), "la cle d'essai creee, le registre d'exemple signe et range");
    check(ws.key().present && ws.key().test && ws.key().id.rfind("xpg-", 0) == 0 && !ws.key().fingerprint.empty(), "la cle : presente, d'essai, son identifiant");
    check(ws.hasRegistry() && ws.registry().number >= 1 && !ws.registry().editions.empty(), "le registre de l'archive : numero, editions");
    check(!ws.createKey("une autre", true).ok, "une deuxieme cle : refusee (jamais remplacee)");

    std::printf("-- les demandes\n");
    const std::string reqA = f.request("pc-a", "Poste essai A"), reqB = f.request("pc-b", "Poste essai B");
    const auto pa = ws.readRequest(reqA);
    check(pa.ok && pa.record.deviceName == "Poste essai A" && !pa.record.fingerprint.empty() && has(pa.record.software, "1.33.0"),
          "l'apercu d'une demande : le nom du PC, l'empreinte, le logiciel (" + pa.why + ")");
    check(!ws.readRequest(U(f.root / "absent.xpgreq")).ok, "une demande absente : refusee");
    const auto noted = ws.recordRequests({reqA, reqB});
    check(noted.ok && ws.records().size() == 2 && ws.dashboard().pendingRequests == 2, "deux demandes notees : en attente (" + noted.text() + ")");
    check(ws.suggestLicenseId() == "XPG-2026-00001", "l'identifiant propose : XPG-2026-00001 (" + ws.suggestLicenseId() + ")");

    std::printf("-- signer une licence\n");
    gui::LicenceDraft d;
    d.licenseId = ws.suggestLicenseId();
    d.customer = "Client d'essai Alpha";
    d.edition = "PROFESSIONAL";
    d.requestPaths = {reqA, reqB};
    d.maxDevices = 3;
    const auto pv = ws.preview(d);
    check(pv.ok && !pv.details.empty() && ws.licences().empty(), "l'apercu : le recapitulatif, rien d'ecrit (" + pv.text() + ")");
    const fs::path out = f.root / "sortie" / "XPG-2026-00001.xpglic";
    fs::create_directories(out.parent_path());
    const auto signedR = ws.signLicence(d, U(out));
    check(signedR.ok && fs::exists(out) && signedR.output == U(out), "signee : le fichier a envoyer (" + signedR.text() + ")");
    check(ws.licences().size() == 1 && ws.licence("XPG-2026-00001") && ws.licence("XPG-2026-00001")->licence.devices.size() == 2,
          "rangee dans l'archive : 2 PC lies");
    const auto* l1 = ws.licence("XPG-2026-00001");
    check(l1 && l1->stateLabel() == "active" && l1->licence.customer == "Client d'essai Alpha" && l1->licence.maxDevices == 3, "son etat : active, le client, 3 PC au plus");
    {
        const auto dash = ws.dashboard();
        check(dash.licences == 1 && dash.activeLicences == 1 && dash.devices == 2 && dash.pendingRequests == 0 && dash.clients == 1 && !dash.byEdition.empty()
                  && !dash.recent.empty(),
              "le tableau de bord : 1 licence active, 2 PC, plus de demande en attente, 1 client");
        const auto clients = ws.clients();
        check(clients.size() == 1 && clients[0].devices == 2 && clients[0].activeLicences == 1, "les clients : Alpha, 2 PC, 1 licence active");
    }
    check(ws.suggestLicenseId() == "XPG-2026-00002", "l'identifiant suivant : XPG-2026-00002");
    check(!ws.signLicence(d, "").ok, "sans fichier de sortie : refuse");
    {
        gui::LicenceDraft bad = d;
        bad.edition = "INCONNUE";
        bad.licenseId = "XPG-2026-00009";
        check(!ws.preview(bad).ok, "une edition hors du registre : refusee");
        gui::LicenceDraft none = d;
        none.requestPaths.clear();
        none.licenseId = "XPG-2026-00010";
        check(!ws.preview(none).ok, "une licence neuve sans demande : refusee");
    }

    std::printf("-- etendre (un PC de plus)\n");
    const std::string reqC = f.request("pc-c", "Poste essai C", "XPG-2026-00001");
    g_now += 60;   // une autre emission (l'archive garde la plus recente)
    ws.reload();
    gui::LicenceDraft ext = d;
    ext.extend = true;
    ext.requestPaths = {reqC};
    const auto extR = ws.signLicence(ext, U(f.root / "sortie" / "etendue.xpglic"));
    check(extR.ok && ws.licence("XPG-2026-00001")->licence.devices.size() == 3, "etendue : 3 PC (" + extR.text() + ")");

    std::printf("-- revoquer\n");
    const std::string pcB = ws.licence("XPG-2026-00001")->licence.devices[1].key;
    check(!ws.revoke("XPG-2026-00001", pcB, "", false).ok, "sans raison : refuse");
    const auto rv = ws.revoke("XPG-2026-00001", pcB, "PC remplace", false);
    check(rv.ok && ws.hasRevocations() && ws.revocations().number == 1 && ws.licence("XPG-2026-00001")->revokedDevices == 1, "un PC revoque : liste n 1 (" + rv.text() + ")");
    check(has(ws.licence("XPG-2026-00001")->stateLabel(), "1 PC"), "l'etat le dit : " + ws.licence("XPG-2026-00001")->stateLabel());
    check(!ws.revoke("XPG-2026-00001", pcB, "encore", false).ok, "le meme PC deux fois : refuse");
    const auto un = ws.unrevoke(0);
    check(un.ok && ws.revocations().number == 2 && ws.revocations().revoked.empty() && ws.licence("XPG-2026-00001")->revokedDevices == 0,
          "la revocation retiree : liste n 2, vide (" + un.text() + ")");
    const auto all = ws.revoke("XPG-2026-00001", "", "Fin de contrat (essai)", false);
    check(all.ok && ws.licence("XPG-2026-00001")->revoked && ws.dashboard().revokedLicences == 1, "toute la licence revoquee");
    const auto exRev = ws.exportRevocations(U(f.root / "sortie" / "revocations.xpgrev"));
    check(exRev.ok && fs::exists(f.root / "sortie" / "revocations.xpgrev"), "la liste exportee pour les clients");

    std::printf("-- exporter, ranger\n");
    const auto ex = ws.exportLicence("XPG-2026-00001", U(f.root / "sortie" / "copie.xpglic"));
    check(ex.ok && readFile(f.root / "sortie" / "copie.xpglic") == readFile(ws.licence("XPG-2026-00001")->path), "la licence exportee : celle de l'archive");
    check(!ws.exportLicence("XPG-AUCUNE", U(f.root / "x.xpglic")).ok, "une licence absente : refusee");
    const std::string reqD = f.request("pc-d", "Poste essai D");
    const auto imp = ws.importFiles({reqD, U(f.root / "sortie" / "copie.xpglic"), U(f.root / "pas-la.xpglic")});
    check(!imp.ok && has(imp.message, "1 demande") && has(imp.message, "1 fichier refus") && imp.details.size() == 3, "ranger : une demande notee, la licence deja la, un absent refuse (" + imp.text() + ")");
    const auto csv = ws.exportCsv(ws.devices(), U(f.root / "sortie" / "pc.csv"));
    check(csv.ok && has(readFile(f.root / "sortie" / "pc.csv"), "Poste essai A"), "le tableau des PC pour Excel");

    std::printf("-- le registre modifie\n");
    lic::Registry reg = ws.registry();
    const auto before = reg.number;
    reg.editions[0].label = "Libre (essai modifie)";
    const auto sr = ws.signRegistry(reg);
    check(sr.ok && ws.registry().number == before + 1 && ws.registry().editions[0].label == "Libre (essai modifie)", "le registre n+1, signe et range (" + sr.text() + ")");

    std::printf("-- l'expiration\n");
    {
        gui::LicenceDraft t;
        t.licenseId = "XPG-2026-00003";
        t.customer = "Client d'essai Beta";
        t.edition = "PROFESSIONAL";
        t.requestPaths = {f.request("pc-e", "Poste essai E")};
        t.durationDays = 10;
        check(ws.signLicence(t, U(f.root / "sortie" / "beta.xpglic")).ok, "une licence de 10 jours");
        check(ws.dashboard().expiringSoon == 1, "elle finit dans les 30 jours : le tableau de bord le dit");
        const auto saved = g_now;
        g_now += 11 * 86400;
        ws.reload();
        check(ws.licence("XPG-2026-00003")->expired && ws.licence("XPG-2026-00003")->stateLabel() == "expir\xC3\xA9" "e", "11 jours apres : expiree");
        g_now = saved;
        ws.reload();
    }

    std::printf("-- la sauvegarde de la cle (Argon2id)\n");
    {
        gui::Background bg;
        const std::string backup = U(f.root / "sortie" / "cle.xpgcle");
        check(!gui::Workspace::acceptablePhrase("court", nullptr), "une phrase trop courte : refusee");
        check(bg.start("Sauvegarde", [&] { return ws.backupKey("phrase d'essai : sept chevaux bleus", backup); }), "le travail lent demarre");
        check(!bg.start("autre", [] { return gui::OpResult::done("x"); }), "un seul travail a la fois");
        std::optional<gui::OpResult> r;
        for (int i = 0; i < 600 && !r; ++i) {
            r = bg.take();
            if (!r) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        check(r && r->ok && fs::exists(backup), "la sauvegarde ecrite, sans bloquer (" + (r ? r->text() : std::string("pas fini")) + ")");
        check(ws.checkBackup(backup, "phrase d'essai : sept chevaux bleus").ok && !ws.checkBackup(backup, "une autre phrase longue").ok, "la bonne phrase l'ouvre, une autre non");
        check(!has(readFile(backup), "sept chevaux"), "la phrase n'est pas dans le fichier");
    }

    std::printf("-- GitHub (sans reseau)\n");
    {
        std::string code;
        check(!ws.githubConfigure("pas un depot", "main", "", "", false, &code).ok, "un depot mal ecrit : refuse");
        const auto c = ws.githubConfigure("editeur-essai/licences-essai", "", "", "", false, &code);
        check(c.ok && ws.github().configured && ws.github().syncKey && code.rfind("XPGS1-", 0) == 0, "configure : la cle de synchro et son code de secours (" + c.text() + ")");
        std::string why;
        check(ws.githubRecoveryCode(&why) == code, "le code de secours se remontre");
        std::string again;
        const auto c2 = ws.githubConfigure("editeur-essai/licences-essai", "main", "", "", false, &again);
        check(c2.ok && again.empty(), "reconfigurer : la cle reste, pas de nouveau code");
        check(ws.githubSaveToken("essai_jeton_ESSAI_APPLICATION_0000000000").ok && ws.github().token, "le jeton enregistre (protege)");
        check(!has(readFile(f.admin / license_admin::github::kTokenFile), "ESSAI_APPLICATION"), "le jeton n'est pas en clair dans son fichier");
        check(ws.githubEraseToken().ok && !ws.github().token, "le jeton retire");
    }

    return finish();
}
