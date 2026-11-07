// =============================================================================
//  tests/admin_workspace_test.cpp - 1.33.0 : le poste Admin vu par l'application
//  (license_admin/gui/Workspace), sans ecran
// -----------------------------------------------------------------------------
//  Un poste d'essai (admin_test_fixture.hpp : une cle d'ESSAI, le registre
//  d'exemple, des PC fictifs) : la cle, les demandes, signer une licence,
//  l'etendre, revoquer un PC puis la licence, retirer une revocation, le
//  registre modifie, ranger des fichiers, le tableau de bord, les clients, la
//  sauvegarde de la cle, le travail lent ; 1.35.0 : le service des licences
//  (sans reseau : son adresse, la cle de synchro de l'archive et son code de
//  secours, une licence en ligne preparee - son code jamais ecrit - et le
//  service injoignable dit tel quel).
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

    std::printf("-- remplacer un PC (1.33.0 : retirer un PC d'une licence)\n");
    {
        g_now += 3600;
        ws.reload();
        const std::string reqF = f.request("pc-f", "Poste essai F"), reqG = f.request("pc-g", "Poste essai G");
        gui::LicenceDraft g;
        g.licenseId = "XPG-2026-00020";
        g.customer = "Client d'essai Gamma";
        g.edition = "PROFESSIONAL";
        g.maxDevices = 2;
        g.requestPaths = {reqF, reqG};
        check(ws.signLicence(g, U(f.root / "sortie" / "gamma.xpglic")).ok, "Gamma : 2 PC sur 2");
        const auto* lg = ws.licence("XPG-2026-00020");
        const auto end = lg->licence.expiresAt;
        const std::string pcF = lg->licence.devices[0].key;
        check(ws.revoke("XPG-2026-00020", pcF, "PC remplace (essai)", false).ok, "le PC F revoque (toutes les emissions)");
        g_now += 3600;
        ws.reload();
        gui::LicenceDraft blocked = g;
        blocked.extend = true;
        blocked.requestPaths = {f.request("pc-h", "Poste essai H")};
        blocked.maxDevices = 3;
        const auto refused = ws.preview(blocked);
        check(!refused.ok && has(refused.message, "retire ce PC"), "etendre sans retirer F : refuse, et le remede est dit (" + refused.message + ")");
        gui::LicenceDraft swap = blocked;
        swap.maxDevices = 2;
        swap.dropDevices = {pcF};
        swap.expiresAt = end;
        const auto swapped = ws.signLicence(swap, U(f.root / "sortie" / "gamma-2.xpglic"));
        lg = ws.licence("XPG-2026-00020");
        check(swapped.ok && lg->licence.devices.size() == 2 && lg->licence.devices[0].key != pcF && lg->licence.expiresAt == end && has(swapped.text(), "PC retir"),
              "F retire, H ajoute, la meme fin (" + swapped.text() + ")");
        g_now += 3600;
        ws.reload();
        const std::string pcG = ws.licence("XPG-2026-00020")->licence.devices[0].key;
        const auto rm = ws.removeDevice("XPG-2026-00020", pcG, U(f.root / "sortie" / "gamma-3.xpglic"));
        lg = ws.licence("XPG-2026-00020");
        check(rm.ok && lg->licence.devices.size() == 1 && lg->licence.expiresAt == end && fs::exists(f.root / "sortie" / "gamma-3.xpglic"),
              "removeDevice : une nouvelle emission sans le PC G, la meme fin (" + rm.text() + ")");
        check(!ws.removeDevice("XPG-2026-00020", lg->licence.devices[0].key, U(f.root / "sortie" / "x.xpglic")).ok, "le dernier PC ne se retire pas (revoquer la licence)");
        check(!ws.removeDevice("XPG-2026-00020", pcG, U(f.root / "sortie" / "y.xpglic")).ok, "un PC qui n'est plus lie : refuse");
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

    std::printf("-- le service des licences (sans reseau)\n");
    {
        check(!ws.service().configured && !ws.hasServiceJob(), "pas encore regle : rien a publier");
        check(!ws.serviceConfigure("http://exemple.invalid").ok, "une adresse http:// vers un autre poste : refusee");
        check(!ws.serviceConfigure("https://exemple.invalid/chemin").ok, "une adresse avec un chemin : refusee");
        const auto c = ws.serviceConfigure("https://xpg-licences.essai.workers.dev/");
        check(c.ok && ws.service().ready() && ws.service().config.url == "https://xpg-licences.essai.workers.dev", "reglee (" + c.text() + ")");
        std::string code;
        const auto k = ws.syncKeyCreate(&code);
        check(k.ok && ws.service().syncKey && code.rfind("XPGS1-", 0) == 0 && ws.service().syncKeyId.rfind("xpgs-", 0) == 0, "la cle de synchro et son code de secours");
        std::string why;
        check(ws.syncRecoveryCode(&why) == code, "le code de secours se remontre");
        std::string again;
        check(!ws.syncKeyCreate(&again).ok && again.empty(), "une seconde cle : refusee (jamais remplacee)");
        check(!foundIn(f.admin, code), "le code de secours n'est ecrit nulle part en clair");
        Fixture f2(source, "ws-service-b");
        check(!f2.ws->syncKeyRestore("XPGS1-FAUX", false).ok, "un faux code : refuse");
        check(f2.ws->syncKeyRestore(code, false).ok && f2.ws->service().syncKeyId == ws.service().syncKeyId, "un autre poste : la meme cle par le code");
        // une licence en ligne preparee : signee, rangee, un code tire ; rien n'est parti
        gui::LicenceDraft od;
        od.online = true;
        od.licenseId = "XPG-2026-00090";
        od.customer = "Client d'essai en ligne";
        od.edition = "PROFESSIONAL";
        od.maxDevices = 3;
        od.durationDays = 365;
        od.leaseDays = 20;
        od.requestPaths = {f.request("pc-online", "Poste essai en ligne")};
        gui::ServiceJob job;
        std::string activation;
        check(!ws.prepareOnlineLicence(od, {}, job, activation).ok && activation.empty(), "une demande de PC avec une licence en ligne : refusee");
        od.requestPaths.clear();
        const auto p = ws.prepareOnlineLicence(od, {}, job, activation);
        check(p.ok && activation.size() == 23 && activation.rfind("XPG-", 0) == 0 && job.codeHash.size() == 64 && job.leaseDays == 20, "preparee : un code, son empreinte (" + p.text() + ")");
        const auto* l = ws.licence("XPG-2026-00090");
        check(l && gui::Workspace::isOnlineLicence(l->licence) && l->licence.devices.empty() && l->licence.maxDevices == 3, "rangee : en ligne, aucun PC");
        check(!foundIn(f.admin, activation), "le code n'est ecrit dans aucun fichier");
        const auto run = ws.serviceRun(job);
        check(!run.ok && has(run.message, "pas de r\xC3\xA9ponse du service"), "le service injoignable : dit tel quel, rien d'invente (" + run.message + ")");
        check(!ws.prepareOnlineLicence(od, {}, job, activation).ok, "le meme identifiant : refuse (il est dans l'archive)");
        gui::LicenceDraft e = od;
        e.extend = true;
        e.durationDays = 500;
        std::string none;
        const auto x = ws.prepareOnlineLicence(e, {}, job, none);
        check(x.ok && none.empty() && job.codeHash.empty() && job.leaseDays == 0, "prolongee : le meme code, le bail du service garde");
        // une licence liee signee avec le service regle : elle part a ses PC (un travail), et un registre aussi
        ws.takeServiceJob();
        const auto s = f.licence("XPG-2026-00091", "Client d'essai lie", "PERSONAL", {f.request("pc-lie", "Poste essai lie")});
        check(s.ok && ws.hasServiceJob(), "une licence liee signee : sa publication attend (" + s.text() + ")");
        const auto j = ws.takeServiceJob();
        check(j && j->kind == gui::ServiceJob::Kind::Licence && j->licenseId == "XPG-2026-00091" && !ws.hasServiceJob(), "le travail : la licence a ses PC");
        // le tableau de bord : jamais releve -> aucun chiffre du service
        const auto dash = ws.dashboard();
        check(dash.serviceConfigured && dash.snapshotAt == 0 && dash.onlineLicences == 0, "le tableau de bord : le service regle, jamais releve");
    }

    return finish();
}
