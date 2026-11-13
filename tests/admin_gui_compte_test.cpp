// =============================================================================
//  tests/admin_gui_compte_test.cpp - 1.47.0 : << MON COMPTE >> ; 1.51.0 : PLUS
//  DE << DEMANDES D'ADMIN >> - des admins invites, signes d'office d'un coup
// -----------------------------------------------------------------------------
//  Dessinees a blanc (TextRenderer, HostRig), puis contre le VRAI service local
//  (tests/service_local.hpp ; sans Node ni wrangler : SAUTE pour cette partie).
//  Sans reseau - Mon compte pour chaque role :
//   - sans cle : les deux chemins (XPG Admin : son code d'invitation ; l'Admin+ :
//     la cle de l'editeur), aucun pouvoir ;
//   - ajout en cours (1.51.0) : l'identifiant, l'empreinte (Copier), son
//     invitation (le nom, le courriel, le profil, accepte le ...), rien a faire ;
//   - l'admin+ : les 13 pouvoirs, ce qu'il fait seul, la derniere fois (le
//     journal de ce poste), Sauvegarder ma cle ; une derogation en cours (un FAUX
//     adopte : l'etat que la coquille relit du service) ;
//   - un admin (Lea) : actif, ajoute dans la liste no 2, la liste no 3 en
//     vigueur, son profil et ses ecarts, l'histoire de ses droits (les listes
//     vues sur son poste), sa derogation ;
//   - un admin retire (Nina) : retire le ..., plus aucun pouvoir ;
//   - la coquille : le bloc d'identite mene a Mon compte ; plus de << Demandes
//     d'admin >> nulle part (1.51.0).
//  Le service local (1.51.0) - des admins invites :
//   - l'admin+ invite Lea (LV2), Hugo (LV1), Nina (LV1) ; tous trois tapent leur
//     code (inscrits) ; << Relever >> dans Administrateurs : UNE liste (numero + 1)
//     les porte tous, avec le profil de leur invitation, la cle de synchro
//     deposee, une ligne << admin_ajouter >> (code d'invitation) chacun ; ils se
//     voient admins ; Mon compte de Lea : actif, son profil, son courriel.
//  Des noms d'essai seulement (Lea, Hugo et Nina Essai, << exemple.invalid >>).
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
#include "../src/license_admin/Edition.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/DataViews.hpp"
#include "../src/ui/widgets/UnfoldCard.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace admintest;
namespace adm = licensing::admins;
namespace json = core::json;
namespace la = license_admin;
namespace dc = deploy::crypto;

namespace {

const std::string kLea = "L\xC3\xA9" "a Essai";
const std::string kHugo = "Hugo Essai";
const std::string kNina = "Nina Essai";

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
std::string draw(ui::Widget& w, float width = 1400.f, float height = 2400.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }
std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(rien)"); }
std::string signedList(const lic::SigningKey& editor, const adm::AdminList& list) {
    return lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(list), editor.publicKey(),
                                          [&editor](std::string_view m) { return editor.sign(m); }));
}
void pinEditor(const fs::path& folder, const lic::SigningKey& editor) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editor.publicKeyHex() + "\"]}\n");
}
// Les listes vues sur un poste (admins-vus.jsonl : une par ligne, son numero et son bloc signe).
void storeSeen(const fs::path& folder, const std::vector<std::pair<int, std::string>>& lists) {
    std::string text;
    for (const auto& [n, block] : lists) {
        json::Value v = json::Value::object();
        v.set("numero", n);
        v.set("bloc", block);
        text += json::write(v, false) + "\n";
    }
    writeFile(folder / "admins-vus.jsonl", text);
}

// Une chaine du journal des admins (comme le service la garde) : quelques lignes signees.
struct Chain {
    std::uint64_t            n{0};
    std::string              prev;
    std::vector<std::string> lines;
};
void addLine(Chain& c, const lic::SigningKey& key, const std::string& action, const std::string& object, const std::string& summary, lic::Seconds when,
             const std::string& pc) {
    adm::JournalEntry e;
    e.when = when;
    e.action = action;
    e.object = object;
    e.summary = summary;
    e.pc = pc;
    e.software = "XPG-License-Admin 1.47.0";
    e.nonce = "0123456789abcdef0123456789abcdef";
    const std::string entry = adm::writeJournalEntry(e);
    json::Value o = json::Value::object();
    o.set("numero", static_cast<std::uint64_t>(++c.n));
    o.set("precedent", c.prev);
    o.set("recu", static_cast<std::int64_t>(when + 2));
    o.set("cle", key.publicKeyHex());
    o.set("entree", entry);
    o.set("signature", dc::hex(key.sign(adm::journalMessage(entry))));
    const std::string line = json::write(o, false);
    c.lines.push_back(line);
    c.prev = dc::sha256Hex(line);
}
void storeChain(const fs::path& folder, const Chain& c) {
    std::string text;
    for (const auto& l : c.lines) text += l + "\n";
    writeFile(folder / "journal-admins.jsonl", text);
    writeFile(folder / "journal-admins-etat.json",
              "{\"format\":\"xpg-journal-admins/1\",\"dernier\":" + std::to_string(c.n) + ",\"empreinte\":\"" + c.prev + "\",\"cle_service\":\"\",\"problemes\":[]}\n");
}

adm::Admin entry(const lic::SigningKey& k, const std::string& name, const std::string& pc, const std::string& profile, std::vector<std::string> rights,
                 lic::Seconds since, lic::Seconds retired = 0) {
    adm::Admin a;
    a.id = k.keyId();
    a.key = k.publicKeyHex();
    a.name = name;
    a.pc = pc;
    a.since = since;
    a.profile = profile;
    a.rights = adm::normalizeRights(rights);
    a.retired = retired;
    return a;
}

// Toute la pile (la coquille), comme la fenetre.
struct ShellRig {
    menu::MenuManager menus{menu::MenuFactory{}};
    gui::Shell*       shell{nullptr};
    TextRenderer      r;
    explicit ShellRig(const fs::path& folder) {
        auto owned = std::make_unique<gui::Shell>(U(folder), [] { return g_now; });
        shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
    }
    std::string paint(float w = 1400.f, float h = 900.f) {
        static const ui::Theme theme = ui::Theme::byName("Light");
        r.size = {w, h};
        menus.applyPending();
        const menu::FrameContext fc{0.016, 1.0, &theme, r.surfaceSize(), 1.f};
        menus.Update(fc);
        r.beginFrame(theme.color.windowBg);
        menus.Render(r, fc);
        r.endFrame();
        return r.all();
    }
    ui::Widget& root() { return *shell->widgetRoot(); }
};

// Une derogation en cours, telle que la coquille la relit du service (GET /admin/derogation) : un FAUX adopte.
gui::Derogation fakeDerogation(const std::string& key, const std::string& name) {
    gui::Derogation d;
    d.known = true;
    d.id = "0123456789abcdef0123456789abcdef";
    d.adminKey = key;
    d.adminName = name;
    d.start = g_now - 600;
    d.end = g_now + 3000;
    d.state = "active";
    d.signatureOk = true;
    d.serverNow = g_now;
    d.readAt = g_now;
    return d;
}

int journalCount(const gui::Workspace& w, const std::string& action) {
    int n = 0;
    for (const auto& r : w.journalLines().rows) n += r.line.parsed.action == action ? 1 : 0;
    return n;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_compte_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    // ------------------------------------------------------------------------------------------------ sans cle
    std::printf("-- Mon compte : sans cle\n");
    {
        Fixture none(source, "compte-sans-cle");
        HostRig rig(none);
        auto page = gui::makeAccountPage(rig.host());
        const std::string t = draw(*page);
        check(page->title() == "Mon compte" && has(t, "Mon compte") && has(t, "Pas encore admin"), "le titre, le role << Pas encore admin >>");
        check(has(t, "Pas encore de cl\xC3\xA9 sur ce poste") && has(t, "pas de cl\xC3\xA9 sur ce poste"), "pas de cle : dit deux fois (l'identifiant, l'etat)");
        check(has(t, "0 sur 13") && has(t, "Aucun pouvoir : pas de cl\xC3\xA9 sur ce poste."), "aucun pouvoir");
        check(has(t, "l'admin+ seul g\xC3\xA8re le service") && has(t, std::string("Admin ") + gui::kAppVersion), "le service : l'admin+ seul ; la version de l'Admin");
        check(!shown(*page, "compte.copier-id") && !shown(*page, "compte.copier-empreinte") && !shown(*page, "compte.sauvegarder"),
              "ni Copier (pas de cle) ni Sauvegarder");
        // 1.49.0 : l'Admin+ sans cle : la cle de l'editeur seulement ; XPG Admin : devenir admin seulement
        check(!shown(*page, "compte.devenir-admin") && click(*page, "compte.creer-cle") && rig.navigations.back() == "securite",
              "l'Admin+ : Creer la cle de l'editeur... -> Parametres de securite (pas Demander)");
        la::setEdition(la::Edition::Classique);
        auto cpage = gui::makeAccountPage(rig.host());
        (void)draw(*cpage);
        const std::string ct = draw(*cpage);
        check(!shown(*cpage, "compte.creer-cle") && has(ct, "Mon code d'invitation") && !has(ct, "Demander \xC3\xA0 devenir admin") &&
                  click(*cpage, "compte.devenir-admin") && rig.navigations.back() == "admins",
              "XPG Admin : Mon code d'invitation... -> Administrateurs (pas la cle de l'editeur)");
        la::setEdition(la::Edition::Plus);
    }

    // ------------------------------------------------------------------------------------------------ en attente
    std::printf("-- Mon compte : ajout en cours (inscrit par son code d'invitation)\n");
    {
        Fixture pf(source, "compte-attente");
        std::string why;
        auto nina = la::createAdminKey(U(pf.admin), {}, &why);
        check(nina != nullptr, "la cle d'admin de Nina (" + why + ")");
        if (nina) {
            const std::string fp = adm::fingerprint(nina->publicKeyHex());
            // l'inscription notee par joinWithInvitation (le nom, le courriel, le profil de l'invitation)
            writeFile(pf.admin / "inscription-admin.json", "{\"format\":\"xpg-inscription-admin/1\",\"nom\":\"" + kNina +
                                                               "\",\"courriel\":\"nina@exemple.invalid\",\"profil\":\"Admin LV1\",\"invitation\":\"inv-0011223344556677\",\"empreinte\":\"" +
                                                               fp + "\",\"inscrit\":" + std::to_string(g_now - 3600) + "}\n");
            pf.ws->reload();
            check(pf.ws->role() == gui::Role::Pending, "ajout en cours");
            HostRig rig(pf);
            auto page = gui::makeAccountPage(rig.host());
            const std::string t = draw(*page);
            check(has(t, "Admin (ajout en cours)") && has(t, kNina) && has(t, "nina@exemple.invalid"), "le role, le nom, le courriel de son invitation");
            check(has(t, nina->keyId()) && has(t, fp.substr(0, 4)) && has(t, fp.substr(25, 4)), "l'identifiant xpg-..., l'empreinte (" + fp + ")");
            check(has(t, "admin, ajout en cours") && has(t, "Code d'invitation accept\xC3\xA9 le") && has(t, "inv-0011223344556677") &&
                      has(t, "rien : d\xC3\xA8s que l'Admin+ est en ligne"),
                  "l'etat : ajout en cours, le code accepte le ..., son invitation, rien a faire");
            check(!has(t, "En attente d'acceptation") && !has(t, "lis ton empreinte") && !has(t, "demande d'admin") && !has(t, "Demande d'admin") && !has(t, "Demander"),
                  "1.51.0 : ni attente d'acceptation, ni empreinte a lire, ni demande d'admin (les droits disent encore << une demande de licence >>)");
            check(click(*page, "compte.copier-empreinte") && g_copied == fp, "Copier l'empreinte : " + g_copied);
            check(click(*page, "compte.copier-id") && g_copied == nina->keyId(), "Copier l'identifiant : " + g_copied);
            check(shown(*page, "compte.attente") && click(*page, "compte.attente") && rig.navigations.back() == "admins", "Ou en est mon ajout -> Administrateurs");
            check(!shown(*page, "compte.devenir-admin") && !shown(*page, "compte.sauvegarder"), "ni le code, ni Sauvegarder");
            check(has(t, "Tes pouvoirs s'ouvrent d\xC3\xA8s que l'Admin+ a sign\xC3\xA9 ta cl\xC3\xA9 (profil \xC2\xAB Admin LV1 \xC2\xBB"), "ses pouvoirs : bientot, le profil de l'invitation");
        }
    }

    // ------------------------------------------------------------------------------------------------ l'admin+ et ses admins
    std::printf("-- Mon compte : l'admin+, un admin, un admin retire\n");
    Fixture fx(source, "compte-plus");
    check(fx.ready(), "le poste de l'admin+ (cle d'essai, registre)");
    Fixture lf(source, "compte-lea");
    Fixture rf(source, "compte-nina-retiree");
    std::string why;
    auto leaKey = la::createAdminKey(U(lf.admin), kLea, &why);
    auto hugoKey = la::createAdminKey(U(fx.root / "hugo"), kHugo, &why);
    auto ninaKey = la::createAdminKey(U(rf.admin), kNina, &why);
    check(leaKey && hugoKey && ninaKey && fx.ws->signingKey(), "les cles d'essai de Lea, Hugo, Nina (" + why + ")");
    if (!leaKey || !hugoKey || !ninaKey || !fx.ws->signingKey()) return finish();
    const lic::SigningKey& editor = *fx.ws->signingKey();
    adm::Profile lv1;
    lv1.name = "Admin LV1";
    lv1.rights = {"licence_creer", "licence_prolonger", "demandes"};
    check(fx.ws->saveProfile(lv1).ok, "le profil << Admin LV1 >>");
    // la liste no 2 : Lea (le profil LV1 tel quel) ; la no 3 : Lea ajustee (+ codes, - prolonger), Hugo, Nina retiree
    adm::AdminList l2;
    l2.number = 2;
    l2.issued = g_now - 10 * 86400;
    l2.admins.push_back(entry(*leaKey, kLea, "PC-COMMERCIAL-02", "Admin LV1", lv1.rights, g_now - 10 * 86400));
    adm::AdminList l3 = l2;
    l3.number = 3;
    l3.issued = g_now - 2 * 86400;
    l3.admins[0].rights = adm::normalizeRights({"licence_creer", "demandes", "codes"});
    l3.admins.push_back(entry(*hugoKey, kHugo, "PORTABLE-HUGO", adm::kAllRightsProfile, adm::allRights(), g_now - 5 * 86400));
    l3.admins.push_back(entry(*ninaKey, kNina, "PC-ATELIER-04", "Admin LV1", {}, g_now - 60 * 86400, g_now - 2 * 86400));
    const std::string b2 = signedList(editor, l2), b3 = signedList(editor, l3);
    writeFile(fx.admin / "admins.xpgadm", b3);
    // le journal de l'admin+ : il a signe une licence ce matin
    Chain chain;
    addLine(chain, editor, "admin_ajouter", kLea, "a ajout\xC3\xA9 l'admin " + kLea, g_now - 10 * 86400, "PC-BUREAU-01");
    addLine(chain, editor, "licence_creer", "XPG-2026-00412", "a sign\xC3\xA9 la licence XPG-2026-00412", g_now - 3600, "PC-BUREAU-01");
    storeChain(fx.admin, chain);
    fx.ws->reload();
    {
        check(fx.ws->role() == gui::Role::Plus && fx.ws->adminList() && fx.ws->adminList()->number == 3, "l'admin+ ; la liste n\xC2\xB0 3");
        HostRig rig(fx);
        auto page = gui::makeAccountPage(rig.host());
        std::string t = draw(*page);
        check(has(t, "Admin+") && has(t, fx.ws->signingKey()->keyId()) && has(t, adm::fingerprint(editor.publicKeyHex()).substr(0, 4)), "admin+ : le role, l'identifiant, l'empreinte");
        check(has(t, "13 sur 13 \xC2\xB7 la boutique aussi") && has(t, "Profil : \xC2\xAB Tous les droits \xC2\xBB"), "les 13 pouvoirs, la boutique ; le profil << Tous les droits >>");
        check(has(t, "Cr\xC3\xA9" "er et signer une licence") && has(t, "Retirer un PC d'une licence") && has(t, "Boutique : livrer, revoir une commande"),
              "les pouvoirs par leur nom");
        check(has(t, "signer une licence neuve") && has(t, "Critique") && has(t, "Moyenne"), "ce que chacun permet ; sa criticite");
        check(has(t, "auj. ") && has(t, "jamais") && has(t, "le journal des admins relev\xC3\xA9 sur ce poste (2 lignes)"),
              "la derniere fois : aujourd'hui (une licence signee), jamais (le reste) ; d'apres le journal de ce poste");
        check(has(t, "CE QUE L'ADMIN+ FAIT SEUL") && has(t, "Le certificat du service") && has(t, "Publier une version, envoyer un fichier"), "ce que l'admin+ fait seul");
        check(has(t, "tu signes la liste n\xC2\xB0 3") && has(t, "toi : l'admin+ seul g\xC3\xA8re le service (aucune d\xC3\xA9rogation en cours)"),
              "l'etat : la liste qu'il signe ; le service : l'admin+ seul");
        check(has(t, "Tes pouvoirs ne changent pas"), "l'histoire : rien a dire pour l'admin+");
        check(shown(*page, "compte.sauvegarder") && click(*page, "compte.sauvegarder") && rig.navigations.back() == "securite", "Sauvegarder ma cle... -> Parametres de securite");
        check(!shown(*page, "compte.devenir-admin") && !shown(*page, "compte.attente"), "ni Demander, ni l'ecran d'attente");
        // une derogation en cours pour Lea (un FAUX : l'etat adopte, comme la coquille le relit du service)
        fx.ws->adoptDerogation(fakeDerogation(leaKey->publicKeyHex(), kLea));
        page->refresh();
        t = draw(*page);
        check(has(t, "d\xC3\xA9rogation en cours pour " + kLea + " : il reste 50 min"), "une derogation en cours (faux) : pour Lea, il reste 50 min");
    }
    {
        writeFile(lf.admin / "admins.xpgadm", b3);
        pinEditor(lf.admin, editor);
        storeSeen(lf.admin, {{2, b2}, {3, b3}});
        fs::copy_file(fx.admin / "profils-admins.json", lf.admin / "profils-admins.json", fs::copy_options::overwrite_existing);
        lf.ws->reload();
        check(lf.ws->role() == gui::Role::Admin && lf.ws->can("codes") && !lf.ws->can("licence_prolonger"), "Lea : admin, ses droits de la liste n\xC2\xB0 3");
        HostRig rig(lf);
        auto page = gui::makeAccountPage(rig.host());
        std::string t = draw(*page);
        check(has(t, kLea) && has(t, "Admin") && has(t, leaKey->keyId()), "Lea : son nom, son role, son identifiant");
        check(has(t, "actif") && has(t, "l'admin+, dans la liste des admins sign\xC3\xA9" "e n\xC2\xB0 2") && has(t, "Liste en vigueur ici") && has(t, "n\xC2\xB0 3"),
              "l'etat : actif, ajoutee par l'admin+ dans la liste n\xC2\xB0 2, la n\xC2\xB0 3 en vigueur");
        check(has(t, "Admin depuis") && has(t, "la liste conna\xC3\xAEt \xC2\xAB PC-COMMERCIAL-02 \xC2\xBB"), "admin depuis ; ce PC (et celui que la liste lui connait)");
        check(has(t, "3 sur 13 \xC2\xB7 la boutique aussi"), "3 pouvoirs sur 13, et la boutique (Accepter une demande + Creer et signer)");
        check(has(t, "Profil \xC2\xAB Admin LV1 \xC2\xBB \xE2\x80\x94 2 \xC3\xA9" "carts") && has(t, "en plus du profil") && has(t, "retir\xC3\xA9 du profil") &&
                  has(t, "dans le profil"),
              "le profil LV1 et ses 2 ecarts (+ codes, - prolonger)");
        check(has(t, "liste n\xC2\xB0 2, par l'admin+ : admin, 3 droits sur 13 (profil \xC2\xAB Admin LV1 \xC2\xBB)") &&
                  has(t, "liste n\xC2\xB0 3, par l'admin+ : + Revoir le code d'activation ; \xE2\x88\x92 Prolonger une licence"),
              "l'histoire de ses droits : les listes n\xC2\xB0 2 et 3");
        check(has(t, "il est chez l'admin+"), "la derniere fois : le journal est chez l'admin+");
        check(has(t, "l'admin+ seul g\xC3\xA8re le service"), "le service : l'admin+ seul");
        check(!shown(*page, "compte.sauvegarder") && !shown(*page, "compte.devenir-admin") && shown(*page, "compte.copier-empreinte"), "Copier ; ni Sauvegarder ni Demander");
        lf.ws->adoptDerogation(fakeDerogation(leaKey->publicKeyHex(), kLea));
        page->refresh();
        t = draw(*page);
        check(has(t, "d\xC3\xA9rogation en cours pour toi : il reste 50 min"), "sa derogation (faux) : pour elle, il reste 50 min");
        // 1.51.0 : plus de page << Demandes d'admin >>
        ShellRig shell(lf.admin);
        (void)shell.paint();
        check(!shown(shell.root(), "nav.demandes-admins") && shown(shell.root(), "nav.compte"), "le menu d'un admin : Mon compte (plus de Demandes d'admin)");
        shell.shell->navigate("demandes-admins");
        check(shell.shell->currentPageId() != "demandes-admins", "<< demandes-admins >> : une page inconnue");
    }
    {
        writeFile(rf.admin / "admins.xpgadm", b3);
        pinEditor(rf.admin, editor);
        rf.ws->reload();
        check(rf.ws->role() == gui::Role::Retired, "Nina : retiree");
        HostRig rig(rf);
        auto page = gui::makeAccountPage(rig.host());
        const std::string t = draw(*page);
        check(has(t, "Admin retir\xC3\xA9") && has(t, "retir\xC3\xA9 \xE2\x80\x94 ta cl\xC3\xA9 ne signe plus rien") && has(t, "Retir\xC3\xA9 le") &&
                  has(t, "par l'admin+ (liste n\xC2\xB0 3)"),
              "retiree : le role, l'etat, retiree le ... par l'admin+");
        check(has(t, "0 sur 13"), "plus aucun pouvoir");
    }
    {
        ShellRig shell(fx.admin);
        (void)shell.paint();
        const std::string m = shell.paint();
        check(shown(shell.root(), "nav.compte") && !shown(shell.root(), "nav.demandes-admins") && !gui::Shell::entry("demandes-admins") && !has(m, "Demandes d'admin"),
              "le menu de l'admin+ : Mon compte ; plus de Demandes d'admin (1.51.0)");
        check(!gui::Shell::plusOnly("compte"), "Mon compte : tous");
        check(click(shell.root(), "shell.compte") && shell.shell->currentPageId() == "compte", "le bloc d'identite (en haut a gauche) -> Mon compte");
        const std::string t = shell.paint();
        check(has(t, "S\xC3\x89" "CURIT\xC3\x89") && has(t, "Mon compte"), "Mon compte : en tete de Securite");
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : trois admins invites, signes d'office d'un coup (plus de demandes d'admin)\n");
    Fixture wf(source, "compte-service");
    check(wf.ready(), "le poste de l'admin+ (sans liste)");
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    xpg_test::LocalService svc({wf.ws->signingKey() ? wf.ws->signingKey()->publicKeyHex() : std::string()}, so);
    if (svc.skipped()) {
        std::printf("SAUTE : %s\n", svc.error().c_str());
        return finish();
    }
    check(svc.ready(), "le service demarre (" + svc.error() + ")");
    if (!svc.ready()) return finish();
    {
        check(wf.ws->serviceConfigure(svc.url()).ok, "l'adresse du service");
        const auto c = wf.ws->serviceCertify({});
        check(c.ok, "la cle du service certifiee (" + c.text() + ")");
        std::string code;
        check(wf.ws->syncKeyCreate(&code).ok, "la cle de synchro");
        adm::Profile p1;
        p1.name = "Admin LV1";
        p1.rights = {"licence_creer", "licence_prolonger", "demandes"};
        adm::Profile p2;
        p2.name = "Admin LV2";
        p2.rights = {"licence_creer", "licence_prolonger", "licence_option", "demandes", "codes", "options"};
        check(wf.ws->saveProfile(p1).ok && wf.ws->saveProfile(p2).ok, "les profils LV1 et LV2");
        wf.ws->reload();
    }
    Fixture lsf(source, "compte-svc-lea"), hsf(source, "compte-svc-hugo"), nsf(source, "compte-svc-nina");
    const struct {
        Fixture*    f;
        std::string name, email, profile;
    } who[] = {{&lsf, kLea, "lea@exemple.invalid", "Admin LV2"}, {&hsf, kHugo, "hugo@exemple.invalid", "Admin LV1"}, {&nsf, kNina, "nina@exemple.invalid", "Admin LV1"}};
    for (const auto& w : who) {
        std::string code;
        check(wf.ws->inviteAdmin(w.name, w.email, w.profile, code).ok && !code.empty(), w.name + " : invite(e), profil " + w.profile);
        check(w.f->ws->serviceConfigure(svc.url()).ok, w.name + " : l'adresse du service");
        const auto r = w.f->ws->joinWithInvitation(code);
        check(r.ok && r.output == "inscrit", w.name + " tape son code : inscrit(e) - " + r.text());
        w.f->ws->reload();
    }
    const std::string leaHex = lsf.ws->adminKey().keyHex, hugoHex = hsf.ws->adminKey().keyHex, ninaHex = nsf.ws->adminKey().keyHex;
    const int number0 = wf.ws->adminList() ? wf.ws->adminList()->number : 0;
    HostRig prig(wf);
    auto page = gui::makeAdminsPage(prig.host());
    prig.refreshers.push_back([&] {
        wf.ws->reload();
        page->refresh();
    });
    {
        check(click(*page, "admins.relever") && lastOk(prig), "Administrateurs, Relever : " + lastText(prig));
        wf.ws->reload();
        page->refresh();
        const auto* list = wf.ws->adminList() ? &*wf.ws->adminList() : nullptr;
        const adm::Admin* a = list ? list->byKey(leaHex) : nullptr;
        const adm::Admin* b = list ? list->byKey(hugoHex) : nullptr;
        const adm::Admin* n = list ? list->byKey(ninaHex) : nullptr;
        check(list && list->number == number0 + 1 && a && b && n && a->active() && b->active() && n->active(),
              "UNE liste signee (n\xC2\xB0 " + std::to_string(number0 + 1) + ") avec Lea, Hugo et Nina, sans rien accepter");
        check(a && a->profile == "Admin LV2" && a->rights.size() == 6 && b && b->profile == "Admin LV1" && b->rights.size() == 3, "leur profil (LV2, LV1) et ses droits");
        check(wf.ws->fetchJournal(nullptr).ok && journalCount(*wf.ws, "admin_ajouter") == 3, "le journal : une ligne << admin_ajouter >> par nouvel admin (3)");
        const std::string t = draw(*page, 1400.f, 900.f);
        check(has(t, kLea) && has(t, kHugo) && has(t, kNina) && !has(t, "ajout en cours") && !has(t, "attente d'acceptation") && !has(t, "Demandes d'admin") &&
                  !has(t, "Refuser la demande"),
              "Administrateurs : les trois, actifs ; aucune demande d'admin (la tuile << Demandes en attente >> : celles de licence)");
    }
    {
        // Lea et Hugo se voient admins (le regard du poste en cours d'ajout)
        for (Fixture* f : {&lsf, &hsf}) {
            gui::AdminRefresh x;
            const auto r = f->ws->refreshAdminState(nullptr, &x);
            f->ws->reload();
            check(r.ok && x.admitted && f->ws->role() == gui::Role::Admin, f->ws->adminKey().id + " se voit admin : " + r.text());
            check(f->ws->service().syncKey, f->ws->adminKey().id + " : la cle de synchro recue (son trousseau)");
        }
        check(lsf.ws->can("codes") && lsf.ws->can("licence_option") && !hsf.ws->can("codes") && hsf.ws->can("demandes"), "chacun ses droits (LV2, LV1)");
        HostRig lrig(lsf);
        auto mine = gui::makeAccountPage(lrig.host());
        const std::string t = draw(*mine);
        check(has(t, "actif") && has(t, "n\xC2\xB0 " + std::to_string(number0 + 1)) && has(t, "lea@exemple.invalid") && has(t, "6 sur 13"),
              "Mon compte de Lea : actif, la liste n\xC2\xB0 " + std::to_string(number0 + 1) + ", son courriel, 6 pouvoirs");
    }
    return finish();
}
