// =============================================================================
//  tests/admin_gui_service_test.cpp - 1.47.0 : SERVICE EN LIGNE, L'ADMIN+ SEUL
//  GERE LE SERVICE ; LA DEROGATION ; LA CONNEXION SIGNEE (docs/ADMINS.md, par. 11)
// -----------------------------------------------------------------------------
//  Dessinee a blanc (admin_gui_harness.hpp), sans reseau :
//    - un admin : << L'admin+ seul gere le service (derogation possible) >>, l'adresse
//      en lecture seule, Deployer, les adresses de secours, cle-publique.txt gris (le
//      bouton dit pourquoi) ; Verifier et les releves restent ;
//    - une derogation en cours (un FAUX relu du service, adopte comme la coquille le
//      fait) : la carte et son compte a rebours, les boutons rouverts ; l'heure passe
//      (l'horloge d'essai) : la page repasse d'elle-meme en lecture seule ;
//    - l'admin+ : la carte << La derogation >>, le dialogue (1, 4, 24, 72 h ; jamais
//      permanente ; son propre compte Cloudflare, jamais un jeton partage), Revoquer
//      seulement quand une court ; Exporter la connexion ;
//    - un poste neuf : la connexion signee prise au chargement (d'ou elle vient, la
//      cle de l'editeur, la cle du service) ; une connexion modifiee : refusee, dite.
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/core/Json.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/ServiceConnection.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace admintest;
namespace adm = licensing::admins;
namespace la = license_admin;
namespace cx = license_admin::connexion;
namespace sv = licensing::service;

namespace {

const std::string kLea = "L\xC3\xA9" "a Essai";

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
bool enabledOf(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->enabled();
}
std::string tooltipOf(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b ? b->tooltip() : std::string();
}
std::string signedList(const lic::SigningKey& editor, const adm::AdminList& list) {
    return lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(list), editor.publicKey(),
                                          [&editor](std::string_view m) { return editor.sign(m); }));
}
void pinEditor(const fs::path& folder, const lic::SigningKey& editor) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editor.publicKeyHex() + "\"]}\n");
}
std::string certFor(const lic::SigningKey& key, const std::string& url) {
    sv::ServiceCert c;
    c.key = std::string(64, 'c');
    c.url = url;
    c.issuedAt = g_now - 60;
    c.expiresAt = g_now + 365 * 86400;
    c.rights = {sv::kRightActivation, sv::kRightFloating, sv::kRightTime, sv::kRightTrial};
    return lic::writeBlock(lic::signBlock(lic::DocKind::ServiceCert, sv::writeCert(c), key.publicKey(), [&key](std::string_view m) { return key.sign(m); }));
}
// La derogation telle que la coquille la relit du service (GET /admin/etat) : un FAUX adopte, sans reseau.
gui::Derogation fakeDerogation(const std::string& key, const std::string& name, lic::Seconds left) {
    gui::Derogation d;
    d.known = true;
    d.id = "0123456789abcdef0123456789abcdef";
    d.adminKey = key;
    d.adminName = name;
    d.start = g_now - 60;
    d.end = g_now + left;
    d.state = "active";
    d.signatureOk = true;
    d.serverNow = g_now;
    d.readAt = g_now;
    return d;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_service_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    // ------------------------------------------------------------------------------------------- l'admin+ et Lea, admin
    Fixture fx(source, "service-plus");
    check(fx.ready(), "le poste de l'admin+ (cle d'essai, registre)");
    Fixture lf(source, "service-lea");
    std::string why;
    auto leaKey = la::createAdminKey(U(lf.admin), kLea, &why);
    check(leaKey && fx.ws->signingKey(), "la cle d'essai de Lea (" + why + ")");
    if (!leaKey || !fx.ws->signingKey()) return finish();
    const lic::SigningKey& editor = *fx.ws->signingKey();
    adm::AdminList list;
    list.number = 1;
    list.issued = g_now - 86400;
    adm::Admin a;
    a.id = leaKey->keyId();
    a.key = leaKey->publicKeyHex();
    a.name = kLea;
    a.pc = "PC-COMMERCIAL-02";
    a.since = g_now - 86400;
    a.profile = "Admin LV1";
    a.rights = adm::normalizeRights({"licence_creer", "demandes", "offres"});
    list.admins.push_back(a);
    const std::string block = signedList(editor, list);
    writeFile(fx.admin / "admins.xpgadm", block);
    writeFile(lf.admin / "admins.xpgadm", block);
    pinEditor(lf.admin, editor);
    for (auto* f : {&fx, &lf}) writeFile(f->admin / "service.json", "{\n  \"format\": \"xpg-service-editeur/1\",\n  \"url\": \"http://127.0.0.1:9\"\n}\n");
    fx.ws->reload();
    lf.ws->reload();
    check(fx.ws->role() == gui::Role::Plus && lf.ws->role() == gui::Role::Admin && lf.ws->service().ready(), "l'admin+ ; Lea, admin ; un service regle (127.0.0.1:9)");

    std::printf("-- un admin : le service en lecture seule\n");
    {
        HostRig rig(lf);
        auto page = gui::makeServicePage(rig.host());
        const std::string t = draw(*page);
        check(has(t, "L'admin+ seul g\xC3\xA8re le service (d\xC3\xA9rogation possible)"), "la carte : << L'admin+ seul gere le service (derogation possible) >>");
        check(has(t, "lecture seule") && has(t, "72 h au plus"), "lecture seule ; une derogation courte (72 h au plus)");
        check(!enabledOf(*page, "service.deployer") && has(tooltipOf(*page, "service.deployer"), "admin+ seul"), "Deployer le service : gris, il dit pourquoi");
        check(!enabledOf(*page, "service.adresse.enregistrer") && has(tooltipOf(*page, "service.adresse.enregistrer"), "admin+ seul"),
              "l'adresse : en lecture seule (Enregistrer gris, il dit pourquoi)");
        check(!enabledOf(*page, "service.cle-publique") && !enabledOf(*page, "service.certifier"), "cle-publique.txt, Certifier : gris");
        check(enabledOf(*page, "service.verifier"), "Verifier (lire l'etat) : permis");
        check(!find<ui::Button>(*page, "service.derogation.accorder") || !page->findById("service.derogation.accorder")->visible() ||
                  !enabledOf(*page, "service.derogation.accorder"),
              "Accorder une derogation : pas pour un admin");
        check(!has(t, "La d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB"), "pas la carte de l'admin+");
        // un clic force sur Deployer : rien ne part
        page->findById("service.deployer")->setEnabled(true);
        (void)click(*page, "service.deployer");
        check(rig.dialogs.empty() && !rig.reports.empty() && has(rig.reports.back().message, "admin+ seul"), "un clic force : refuse, aucun dialogue");
    }

    std::printf("-- un admin sous derogation : le compte a rebours, puis la fin d'elle-meme\n");
    {
        HostRig rig(lf);
        lf.ws->adoptDerogation(fakeDerogation(leaKey->publicKeyHex(), kLea, 3000));
        check(lf.ws->mayManageService(), "la derogation en cours : Lea gere le service");
        auto page = gui::makeServicePage(rig.host());
        std::string t = draw(*page);
        check(has(t, "Ta d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB") && has(t, "il reste 50 min"), "la carte : il reste 50 min");
        check(has(t, "TON compte") && has(t, "jamais avec un jeton partag"), "Cloudflare : son propre compte, jamais un jeton partage");
        check(enabledOf(*page, "service.deployer") && enabledOf(*page, "service.adresse.enregistrer"), "Deployer, l'adresse : rouverts");
        check(!enabledOf(*page, "service.certifier"), "Certifier : toujours a l'admin+ (sa cle)");
        g_now += 1200;
        t = draw(*page);
        check(has(t, "il reste 30 min"), "20 minutes plus tard : il reste 30 min");
        g_now += 1900;
        (void)draw(*page);   // l'image ou la page se relit (la fin vue), puis la suivante
        t = draw(*page);
        check(!lf.ws->mayManageService() && has(t, "L'admin+ seul g\xC3\xA8re le service (d\xC3\xA9rogation possible)") && !enabledOf(*page, "service.deployer"),
              "a la fin : la page repasse d'elle-meme en lecture seule");
        check(has(t, "termin\xC3\xA9" "e le"), "sa derniere derogation : terminee le ...");
        check(has(lf.ws->whyNotService(), "termin"), "le bouton dit : terminee (" + lf.ws->whyNotService() + ")");
        g_now -= 3100;
        gui::Derogation r = fakeDerogation(leaKey->publicKeyHex(), kLea, 3000);
        r.revoked = g_now;
        r.state = "revoquee";
        lf.ws->adoptDerogation(r);
        check(!lf.ws->mayManageService() && has(lf.ws->whyNotService(), "voqu"), "revoquee : lecture seule (" + lf.ws->whyNotService() + ")");
        lf.ws->adoptDerogation(gui::Derogation{});
    }

    std::printf("-- l'admin+ : la derogation, le dialogue\n");
    {
        HostRig rig(fx);
        auto page = gui::makeServicePage(rig.host());
        std::string t = draw(*page);
        check(has(t, "La d\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB") && has(t, "jamais permanente"), "la carte de l'admin+ : jamais permanente");
        check(enabledOf(*page, "service.derogation.accorder") && !page->findById("service.derogation.revoquer")->visible(),
              "Accorder... permis ; Revoquer cache (aucune en cours)");
        check(!has(t, "L'admin+ seul g\xC3\xA8re le service (d\xC3\xA9rogation possible)") && enabledOf(*page, "service.deployer"), "l'admin+ gere le service");
        check(click(*page, "service.derogation.accorder") && rig.dialogs.size() == 1, "Accorder une derogation... : le dialogue");
        if (!rig.dialogs.empty()) {
            auto& dlg = *rig.dialogs.back();
            const std::string d = drawMenu(dlg);
            ui::Widget& root = *dlg.widgetRoot();
            check(has(d, "D\xC3\xA9rogation \xC2\xAB G\xC3\xA9rer le service \xC2\xBB") && has(d, "72 h au plus, jamais permanente"), "le titre ; 72 h au plus, jamais permanente");
            check(root.findById("derogation.duree.1") && root.findById("derogation.duree.4") && root.findById("derogation.duree.24") &&
                      root.findById("derogation.duree.72") && !root.findById("derogation.duree.0"),
                  "les durees : 1 h, 4 h, 24 h, 72 h - et rien d'autre");
            check(has(d, "SON propre compte") && has(d, "Jamais un jeton partag"), "Cloudflare : son propre compte, jamais un jeton partage");
            check(has(d, "Jamais : certifier") && has(d, "Brancher Stripe"), "ce qu'elle n'ouvre jamais : certifier, la sauvegarde, Stripe");
            auto* who = find<ui::DropDown>(root, "derogation.admin");
            check(who && who->selectedItem() && who->selectedItem()->value == leaKey->publicKeyHex(), "l'admin : Lea (la liste des admins actifs)");
            check(enabledOf(root, "derogation.accorder") && !root.findById("derogation.revoquer")->visible(), "Accorder ; pas de Revoquer");
        }
        fx.ws->adoptDerogation(fakeDerogation(leaKey->publicKeyHex(), kLea, 3600 * 4 - 60));
        page->refresh();
        t = draw(*page);
        check(has(t, "En cours") && has(t, kLea + " \xE2\x80\x94 il reste 3 h 59") && enabledOf(*page, "service.derogation.revoquer"),
              "en cours : Lea, il reste 3 h 59 ; Revoquer maintenant");
        fx.ws->adoptDerogation(gui::Derogation{});
    }

    std::printf("-- la connexion au service : un poste neuf\n");
    {
        Fixture nf(source, "service-neuf");
        const fs::path beside = nf.root / "programme";
        writeFile(beside / cx::kFileName, certFor(editor, "https://xpg-licences.essai.workers.dev"));
        pinEditor(nf.admin, editor);   // (l'Admin livre a la cle de l'editeur compilee)
        cx::Places p;
        p.beside = {U(beside)};
        cx::setPlaces(p);
        nf.ws->reload();
        cx::setPlaces({});
        check(nf.ws->service().configured && nf.ws->service().hasConnection, "la connexion signee prise au chargement");
        HostRig rig(nf);
        auto page = gui::makeServicePage(rig.host());
        const std::string t = draw(*page);
        check(has(t, "La connexion au service") && has(t, "programme") && has(t, editor.keyId()) && has(t, "https://xpg-licences.essai.workers.dev"),
              "d'ou elle vient, signee par la cle de l'editeur, l'adresse");
        check(enabledOf(*page, "service.adresse.enregistrer"), "un poste sans role peut encore taper une adresse");

        Fixture bf(source, "service-modifiee");
        std::string bad = certFor(editor, "https://xpg-licences.essai.workers.dev");
        bad.replace(bad.find("essai.workers"), 5, "pirat");
        const fs::path b2 = bf.root / "programme";
        writeFile(b2 / cx::kFileName, bad);
        pinEditor(bf.admin, editor);
        cx::Places q;
        q.beside = {U(b2)};
        cx::setPlaces(q);
        bf.ws->reload();
        cx::setPlaces({});
        HostRig rig2(bf);
        auto page2 = gui::makeServicePage(rig2.host());
        const std::string t2 = draw(*page2);
        check(!bf.ws->service().configured && has(t2, "Refus\xC3\xA9" "e : ") && has(t2, "modifi") && has(t2, "Aucune connexion sign"),
              "une connexion modifiee : refusee, et la page le dit");
    }
    return finish();
}
