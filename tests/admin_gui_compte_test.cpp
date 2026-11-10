// =============================================================================
//  tests/admin_gui_compte_test.cpp - 1.47.0 : << MON COMPTE >> ET LES
//  << DEMANDES D'ADMIN >> (accepter plusieurs demandes d'un coup)
// -----------------------------------------------------------------------------
//  Dessinees a blanc (TextRenderer, HostRig), puis contre le VRAI service local
//  (tests/service_local.hpp ; sans Node ni wrangler : SAUTE pour cette partie).
//  Sans reseau - Mon compte pour chaque role :
//   - sans cle : les deux chemins (Demander a devenir admin, la cle de
//     l'editeur), aucun pouvoir ;
//   - en attente : l'identifiant, l'empreinte en 6 groupes (Copier), sa demande ;
//   - l'admin+ : les 13 pouvoirs, ce qu'il fait seul, la derniere fois (le
//     journal de ce poste), Sauvegarder ma cle ; une derogation en cours (un FAUX
//     adopte : l'etat que la coquille relit du service) ;
//   - un admin (Lea) : actif, ajoute dans la liste n° 2, la liste n° 3 en
//     vigueur, son profil et ses ecarts, l'histoire de ses droits (les listes
//     vues sur son poste), sa derogation ;
//   - un admin retire (Nina) : retire le ..., plus aucun pouvoir ;
//   - la coquille : le bloc d'identite mene a Mon compte ; << Demandes d'admin >>
//     dans le menu de l'admin+ seulement.
//  Le service local - les Demandes d'admin :
//   - Lea (invitee), Hugo et Nina demandent ; la liste (3 en attente), la barre
//     de filtres, le clic droit (l'ordre de la regle), la ligne depliee ;
//   - les 3 choisies : UNE fenetre, une case par empreinte ; Nina pas cochee :
//     rien ne se signe (le bouton grise ; Workspace::acceptAdmins refuse aussi) ;
//     Nina retiree du lot -> << Accepter les 2 >> : UNE liste (numero + 1) avec
//     Lea et Hugo, leur profil, la cle de synchro deposee, une ligne au journal
//     chacun ; Lea et Hugo se voient admins ;
//   - Nina refusee (une raison) ; puis deux demandes de Nina (deux PC) refusees
//     d'un coup.
//  Des noms d'essai seulement (Lea, Hugo et Nina Essai, << exemple.invalid >>).
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/Admin.hpp"
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
std::string drawMenu(menu::IMenu& m, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paintMenu(m, r, width, height);
}
bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}
bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }
std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(rien)"); }
bool closed(menu::IMenu& m) { return m.widgetRoot() && !m.widgetRoot()->visible(); }
bool setCheck(ui::Widget& root, std::string_view id, bool on) {
    auto* c = find<ui::Checkbox>(root, id);
    if (!c || !c->enabled()) return false;
    c->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    return true;
}
std::string pickedValue(ui::Widget& root, std::string_view id) {
    auto* d = find<ui::DropDown>(root, id);
    const auto* it = d ? d->selectedItem() : nullptr;
    return it ? it->value : std::string("?");
}
std::size_t rowsOf(ui::Widget& page, std::string_view id) {
    auto* t = find<ui::TableView>(page, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}
int rowOf(ui::TableView& t, std::size_t col, const std::string& text) {
    for (std::size_t i = 0; t.model() && i < t.model()->rowCount(); ++i)
        if (t.model()->cellText(static_cast<ui::RowIndex>(i), col) == text) return static_cast<int>(i);
    return -1;
}
std::vector<ui::PopupMenu::Item> rightClick(ui::TableView& t, std::vector<ui::RowIndex> rows) {
    t.selectModelRows(std::move(rows));
    t.openContextMenu({200.f, 200.f});
    return t.contextMenu() ? t.contextMenu()->items() : std::vector<ui::PopupMenu::Item>{};
}
std::string topLabels(const std::vector<ui::PopupMenu::Item>& items) {
    std::string s;
    for (const auto& it : items) s += it.separator ? std::string("|") : it.label + ";";
    return s;
}
int idOf(const std::vector<ui::PopupMenu::Item>& items, const std::string& parent, const std::string& start) {
    for (const auto& it : items) {
        if (parent.empty() && it.label.rfind(start, 0) == 0) return it.enabled ? it.id : -1;
        if (it.label == parent)
            for (const auto& c : it.children)
                if (c.label.rfind(start, 0) == 0) return c.enabled ? c.id : -1;
    }
    return -1;
}
const ui::PopupMenu::Item* itemOf(const std::vector<ui::PopupMenu::Item>& items, const std::string& start) {
    for (const auto& it : items)
        if (!it.separator && it.label.rfind(start, 0) == 0) return &it;
    return nullptr;
}

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
        check(click(*page, "compte.devenir-admin") && rig.navigations.back() == "admins", "Demander a devenir admin... -> Administrateurs");
        check(click(*page, "compte.creer-cle") && rig.navigations.back() == "securite", "Creer la cle de l'editeur... -> Parametres de securite");
    }

    // ------------------------------------------------------------------------------------------------ en attente
    std::printf("-- Mon compte : en attente\n");
    {
        Fixture pf(source, "compte-attente");
        std::string why;
        auto nina = la::createAdminKey(U(pf.admin), kNina, &why);
        check(nina != nullptr, "la cle d'admin de Nina (" + why + ")");
        if (nina) {
            const std::string fp = adm::fingerprint(nina->publicKeyHex());
            writeFile(pf.admin / "demande-admin.json", "{\"nom\":\"" + kNina + "\",\"courriel\":\"nina@exemple.invalid\",\"empreinte\":\"" + fp +
                                                           "\",\"envoyee\":" + std::to_string(g_now - 3600) + "}\n");
            pf.ws->reload();
            check(pf.ws->role() == gui::Role::Pending, "en attente");
            HostRig rig(pf);
            auto page = gui::makeAccountPage(rig.host());
            const std::string t = draw(*page);
            check(has(t, "En attente d'acceptation") && has(t, kNina) && has(t, "nina@exemple.invalid"), "le role, le nom, le courriel de sa demande");
            check(has(t, nina->keyId()) && has(t, fp.substr(0, 4)) && has(t, fp.substr(25, 4)), "l'identifiant xpg-..., l'empreinte en 6 groupes (" + fp + ")");
            check(has(t, "en attente d'acceptation par l'admin+") && has(t, "Demande envoy\xC3\xA9" "e le"), "l'etat : en attente, la demande envoyee le");
            check(click(*page, "compte.copier-empreinte") && g_copied == fp, "Copier l'empreinte : " + g_copied);
            check(click(*page, "compte.copier-id") && g_copied == nina->keyId(), "Copier l'identifiant : " + g_copied);
            check(shown(*page, "compte.attente") && click(*page, "compte.attente") && rig.navigations.back() == "admins", "Voir ma demande -> l'ecran d'attente");
            check(!shown(*page, "compte.devenir-admin") && !shown(*page, "compte.sauvegarder"), "ni Demander, ni Sauvegarder");
            check(has(t, "Aucun pouvoir tant que l'admin+ n'a pas accept\xC3\xA9"), "aucun pouvoir tant qu'il n'a pas accepte");
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
    // la liste n° 2 : Lea (le profil LV1 tel quel) ; la n° 3 : Lea ajustee (+ codes, - prolonger), Hugo, Nina retiree
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
        // Demandes d'admin : l'admin+ seul
        auto rp = gui::makeAdminRequestsPage(rig.host());
        const std::string r = draw(*rp, 1400.f, 900.f);
        check(has(r, "Seul l'admin+ accepte un admin") && !shown(*rp, "demandes-admins.accepter") && !shown(*rp, "demandes-admins.relever"),
              "un admin : la page Demandes d'admin dit que c'est l'admin+, sans bouton");
        ShellRig shell(lf.admin);
        (void)shell.paint();
        check(!shown(shell.root(), "nav.demandes-admins") && shown(shell.root(), "nav.compte"), "le menu d'un admin : Mon compte, pas Demandes d'admin");
        shell.shell->navigate("demandes-admins");
        check(shell.shell->currentPageId() != "demandes-admins", "Demandes d'admin : refuse a un admin");
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
        check(shown(shell.root(), "nav.compte") && shown(shell.root(), "nav.demandes-admins"), "le menu de l'admin+ : Mon compte, Demandes d'admin");
        check(gui::Shell::plusOnly("demandes-admins") && !gui::Shell::plusOnly("compte"), "Demandes d'admin : l'admin+ seul ; Mon compte : tous");
        check(click(shell.root(), "shell.compte") && shell.shell->currentPageId() == "compte", "le bloc d'identite (en haut a gauche) -> Mon compte");
        const std::string t = shell.paint();
        check(has(t, "S\xC3\x89" "CURIT\xC3\x89") && has(t, "Mon compte"), "Mon compte : en tete de Securite");
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : trois demandes, deux acceptees d'un coup, une refusee\n");
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
        check(wf.ws->noteAdminInvitation({kLea, "lea@exemple.invalid", "Admin LV2", "lien", g_now - 86400}).ok, "Lea invitee (profil LV2 prevu)");
    }
    Fixture lsf(source, "compte-svc-lea"), hsf(source, "compte-svc-hugo"), nsf(source, "compte-svc-nina");
    const struct {
        Fixture*    f;
        std::string name, email;
    } who[] = {{&lsf, kLea, "lea@exemple.invalid"}, {&hsf, kHugo, "hugo@exemple.invalid"}, {&nsf, kNina, "nina@exemple.invalid"}};
    for (const auto& w : who) {
        check(w.f->ws->serviceConfigure(svc.url()).ok, w.name + " : l'adresse du service");
        w.f->ws->reload();
        std::string fp;
        const auto r = w.f->ws->requestAdmin(w.name, w.email, &fp);
        check(r.ok && !fp.empty(), w.name + " demande a devenir admin : " + r.text());
        w.f->ws->reload();
    }
    const std::string leaHex = lsf.ws->adminKey().keyHex, hugoHex = hsf.ws->adminKey().keyHex, ninaHex = nsf.ws->adminKey().keyHex;
    HostRig prig(wf);
    auto page = gui::makeAdminRequestsPage(prig.host());   // releve a l'arrivee
    prig.refreshers.push_back([&] {
        wf.ws->reload();
        page->refresh();
    });
    auto* table = find<ui::TableView>(*page, "demandes-admins.table");
    auto* bar = find<ui::FilterBar>(*page, "demandes-admins");
    check(table && bar, "la table, la barre de filtres");
    if (!table || !bar) return finish();
    {
        const std::string t = draw(*page, 1400.f, 900.f);
        check(rowsOf(*page, "demandes-admins.table") == 3 && has(t, kLea) && has(t, kHugo) && has(t, kNina), "la liste : les 3 demandes");
        check(has(t, "3 demandes en attente") && has(t, "1 invitation not\xC3\xA9" "e sur ce poste"), "3 en attente ; 1 invitation notee");
        const int il = rowOf(*table, 1, kLea), ih = rowOf(*table, 1, kHugo);
        check(il >= 0 && ih >= 0 && has(table->model()->cellText(static_cast<ui::RowIndex>(il), 6), "invit\xC3\xA9 le ") &&
                  has(table->model()->cellText(static_cast<ui::RowIndex>(il), 6), "Admin LV2") &&
                  table->model()->cellText(static_cast<ui::RowIndex>(ih), 6) == "sans invitation",
              "l'invitation : Lea << invitee le ... (Admin LV2) >>, Hugo << sans invitation >>");
        check(il >= 0 && table->model()->cellText(static_cast<ui::RowIndex>(il), 5) == adm::fingerprint(leaHex).substr(0, 9) + "\xE2\x80\xA6" &&
                  table->model()->cellText(static_cast<ui::RowIndex>(il), 7) == "En attente" && table->model()->cellText(static_cast<ui::RowIndex>(il), 2) == "lea@exemple.invalid",
              "l'empreinte courte, l'etat, le courriel");
        // la barre de filtres
        bar->setSearch("Hugo");
        check(rowsOf(*page, "demandes-admins.table") == 1, "chercher << Hugo >> : 1 ligne");
        bar->setSearch("");
        bar->filterOn(9, "sans invitation");
        check(rowsOf(*page, "demandes-admins.table") == 2, "Invitation : sans invitation -> 2 lignes (Hugo, Nina)");
        bar->clearField(9);
        check(rowsOf(*page, "demandes-admins.table") == 3, "la puce enlevee : les 3");
        // le clic droit (une ligne) : l'ordre de la regle
        const int in = rowOf(*table, 1, kNina);
        auto items = rightClick(*table, {static_cast<ui::RowIndex>(in)});
        const std::string order = topLabels(items);
        check(order.rfind("Ouvrir la demande;|Accepter\xE2\x80\xA6;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);|Refuser\xE2\x80\xA6;", 0) == 0,
              "le clic droit : Ouvrir / Accepter... / Aller a, Filtrer sur / Copier, Exporter / Refuser... : " + order);
        const auto* refuse = itemOf(items, "Refuser");
        check(refuse && refuse->danger, "Refuser... : en rouge, a part");
        const int cp = idOf(items, "Copier", "L'empreinte");
        if (cp >= 0) table->contextAction->emit(cp);
        check(cp >= 0 && g_copied == adm::fingerprint(ninaHex), "Copier > L'empreinte : " + g_copied);
        // plusieurs lignes
        items = rightClick(*table, {0, 1, 2});
        const std::string many = topLabels(items);
        check(has(many, "Accepter les 3 demandes\xE2\x80\xA6;") && has(many, "Refuser les 3 demandes\xE2\x80\xA6;") && !has(many, "Aller \xC3\xA0"),
              "trois lignes : Accepter les 3, Refuser les 3 : " + many);
        // la ligne depliee
        check(table->setUnfolded(static_cast<ui::RowIndex>(il), true), "deplier la demande de Lea");
        auto* card = dynamic_cast<ui::UnfoldCard*>(table->unfoldWidget(static_cast<ui::RowIndex>(il)));
        const std::string ct = card ? card->text() : std::string();
        check(card && has(ct, "L'EMPREINTE") && has(ct, "L'INVITATION") && has(ct, "profil pr\xC3\xA9vu : **Admin LV2**") && has(ct, lsf.ws->adminKey().id),
              "depliee : l'empreinte (en grand), la demande, l'invitation et son profil : " + ct);
        (void)table->setUnfolded(static_cast<ui::RowIndex>(il), false);
    }
    const int number0 = wf.ws->adminList() ? wf.ws->adminList()->number : 0;
    {
        // les trois choisies : UNE fenetre
        table->selectModelRows({0, 1, 2});
        (void)draw(*page, 1400.f, 900.f);
        auto* acc = find<ui::Button>(*page, "demandes-admins.accepter");
        check(acc && acc->text() == "Accepter 3 demandes\xE2\x80\xA6" && acc->enabled(), "<< Accepter 3 demandes... >>");
        auto* ref = find<ui::Button>(*page, "demandes-admins.refuser");
        check(ref && ref->text() == "Refuser 3 demandes\xE2\x80\xA6", "<< Refuser 3 demandes... >>");
        std::vector<std::string> order;   // l'ordre des demandes dans la fenetre : celui des lignes choisies
        for (const auto r : table->selectedModelRows()) order.push_back(table->model()->cellText(r, 1));
        const auto at = [&](const std::string& name) {
            for (std::size_t i = 0; i < order.size(); ++i)
                if (order[i] == name) return std::to_string(i);
            return std::string("?");
        };
        const std::size_t d0 = prig.dialogs.size();
        check(click(*page, "demandes-admins.accepter") && prig.dialogs.size() == d0 + 1, "la fenetre << Accepter 3 demandes d'admin >>");
        if (prig.dialogs.size() == d0 + 1) {
            auto& dlg = *prig.dialogs.back();
            ui::Widget& root = *dlg.widgetRoot();
            std::string d = drawMenu(dlg);
            check(has(d, "Accepter 3 demandes d'admin") && has(d, "(n\xC2\xB0 " + std::to_string(number0 + 1) + ")"), "le titre ; la liste qui sera signee (n\xC2\xB0 + 1)");
            check(has(d, adm::fingerprint(leaHex).substr(0, 4)) && has(d, adm::fingerprint(hugoHex).substr(15, 4)) && has(d, adm::fingerprint(ninaHex).substr(25, 4)),
                  "chaque empreinte en 6 groupes");
            check(has(d, "Invitation : invit\xC3\xA9 le") && has(d, "Sans invitation : v\xC3\xA9rifie bien qui c'est."), "l'invitation de chacun");
            check(pickedValue(root, "demandes-admins.lot.profil." + at(kLea)) == "Admin LV2" && pickedValue(root, "demandes-admins.lot.profil." + at(kHugo)) == "Admin LV1" &&
                      pickedValue(root, "demandes-admins.lot.profil." + at(kNina)) == "Admin LV1",
                  "le profil propose : celui de l'invitation (Lea : LV2), sinon LV1");
            auto* ok = find<ui::Button>(root, "demandes-admins.lot.ok");
            check(ok && !ok->enabled() && ok->text() == "Accepter les 3", "<< Accepter les 3 >> : grise tant qu'aucune case");
            check(setCheck(root, "demandes-admins.lot.verifie." + at(kLea), true) && setCheck(root, "demandes-admins.lot.verifie." + at(kHugo), true),
                  "les empreintes de Lea et Hugo verifiees");
            d = drawMenu(dlg);
            check(ok && !ok->enabled() && !click(root, "demandes-admins.lot.ok") && has(d, "2 sur 3 v\xC3\xA9rifi\xC3\xA9" "es"),
                  "celle de Nina pas cochee : rien ne se signe (le bouton reste grise ; << 2 sur 3 verifiees >>)");
            // la meme regle dans le Workspace : une case vide, rien n'est signe
            const auto bad = wf.ws->acceptAdmins({{leaHex, "Admin LV2", true}, {hugoHex, "Admin LV1", true}, {ninaHex, "Admin LV1", false}});
            wf.ws->reload();
            check(!bad.ok && has(bad.message, "rien n'est sign\xC3\xA9") && (wf.ws->adminList() ? wf.ws->adminList()->number : 0) == number0,
                  "Workspace::acceptAdmins avec une case vide : refuse, aucune liste signee : " + bad.message);
            check(click(root, "demandes-admins.lot.retirer." + at(kNina)), "Nina retiree du lot");
            d = drawMenu(dlg);
            check(ok && ok->enabled() && ok->text() == "Accepter les 2" && has(d, "Accepter 2 demandes d'admin"), "<< Accepter les 2 >>");
            check(click(root, "demandes-admins.lot.ok") && lastOk(prig) && closed(dlg), "Lea et Hugo acceptes d'un coup : " + lastText(prig));
        }
        wf.ws->reload();
        const auto* list = wf.ws->adminList() ? &*wf.ws->adminList() : nullptr;
        const adm::Admin* a = list ? list->byKey(leaHex) : nullptr;
        const adm::Admin* b = list ? list->byKey(hugoHex) : nullptr;
        check(list && list->number == number0 + 1 && a && b && a->active() && b->active() && !list->byKey(ninaHex),
              "UNE liste signee (n\xC2\xB0 " + std::to_string(number0 + 1) + ") avec Lea et Hugo, sans Nina");
        check(a && a->profile == "Admin LV2" && a->rights.size() == 6 && b && b->profile == "Admin LV1" && b->rights.size() == 3, "leur profil (LV2, LV1) et ses droits");
        check(wf.ws->fetchJournal(nullptr).ok && journalCount(*wf.ws, "admin_ajouter") == 2, "le journal : une ligne << admin_ajouter >> par nouvel admin (2)");
        const std::string t = draw(*page, 1400.f, 900.f);
        check(rowsOf(*page, "demandes-admins.table") == 1 && has(t, "1 demande en attente"), "la liste relevee : Nina seule en attente");
        bar->clearAll();
        check(rowsOf(*page, "demandes-admins.table") == 3 && table->model()->cellText(static_cast<ui::RowIndex>(rowOf(*table, 1, kLea)), 8) == leaHex &&
                  has(table->model()->cellText(static_cast<ui::RowIndex>(rowOf(*table, 1, kLea)), 7), "Accept\xC3\xA9" "e"),
              "toutes : Lea et Hugo << Acceptee >>");
        bar->setValues(8, {"En attente"});
    }
    {
        // Lea et Hugo se voient admins (le regard du poste en attente)
        for (Fixture* f : {&lsf, &hsf}) {
            gui::AdminRefresh x;
            const auto r = f->ws->refreshAdminState(nullptr, &x);
            f->ws->reload();
            check(r.ok && x.accepted && f->ws->role() == gui::Role::Admin, f->ws->adminKey().name + " se voit admin : " + r.text());
            check(f->ws->service().syncKey, f->ws->adminKey().name + " : la cle de synchro recue (son trousseau)");
        }
        check(lsf.ws->can("codes") && lsf.ws->can("licence_option") && !hsf.ws->can("codes") && hsf.ws->can("demandes"), "chacun ses droits (LV2, LV1)");
        HostRig lrig(lsf);
        auto mine = gui::makeAccountPage(lrig.host());
        const std::string t = draw(*mine);
        check(has(t, "actif") && has(t, "n\xC2\xB0 " + std::to_string(number0 + 1)) && has(t, "lea@exemple.invalid") && has(t, "6 sur 13"),
              "Mon compte de Lea : actif, la liste n\xC2\xB0 " + std::to_string(number0 + 1) + ", son courriel, 6 pouvoirs");
    }
    {
        // Nina refusee, avec une raison
        const int in = rowOf(*table, 1, kNina);
        check(in >= 0, "la demande de Nina");
        table->selectModelRows({static_cast<ui::RowIndex>(in)});
        auto* ref = find<ui::Button>(*page, "demandes-admins.refuser");
        check(ref && ref->text() == "Refuser\xE2\x80\xA6" && ref->enabled(), "<< Refuser... >>");
        const std::size_t d0 = prig.dialogs.size();
        check(click(*page, "demandes-admins.refuser") && prig.dialogs.size() == d0 + 1, "la fenetre << Refuser >>");
        if (prig.dialogs.size() == d0 + 1) {
            auto& dlg = *prig.dialogs.back();
            const std::string d = drawMenu(dlg);
            check(has(d, "Refuser la demande de " + kNina) && has(d, "Raison (facultative)"), "le titre ; la raison facultative");
            check(type(*dlg.widgetRoot(), "demandes-admins.refus.raison", "pas pour l'instant") && click(*dlg.widgetRoot(), "demandes-admins.refus.ok") && lastOk(prig) &&
                      closed(dlg),
                  "refusee : " + lastText(prig));
        }
        std::string st;
        check(nsf.ws->adminRequestState(st).ok && st == "refusee", "Nina voit << refusee >> (" + st + ")");
        check(wf.ws->fetchJournal(nullptr).ok && journalCount(*wf.ws, "demande_refuser") == 1, "le journal : une ligne << demande_refuser >>");
    }
    {
        // Nina demande de nouveau, de deux PC : les deux refusees d'un coup
        Fixture n2f(source, "compte-svc-nina-portable");
        check(n2f.ws->serviceConfigure(svc.url()).ok, "le second PC de Nina : l'adresse du service");
        n2f.ws->reload();
        check(nsf.ws->requestAdmin(kNina, "nina@exemple.invalid").ok && n2f.ws->requestAdmin(kNina, "nina@exemple.invalid").ok, "Nina demande de nouveau, de deux PC");
        check(click(*page, "demandes-admins.relever") && lastOk(prig), "Relever : " + lastText(prig));
        check(rowsOf(*page, "demandes-admins.table") == 2, "2 demandes en attente (Nina, deux PC)");
        table->selectModelRows({0, 1});
        auto* ref = find<ui::Button>(*page, "demandes-admins.refuser");
        check(ref && ref->text() == "Refuser 2 demandes\xE2\x80\xA6" && ref->enabled(), "<< Refuser 2 demandes... >>");
        const std::size_t d0 = prig.dialogs.size();
        check(click(*page, "demandes-admins.refuser") && prig.dialogs.size() == d0 + 1, "la fenetre << Refuser 2 demandes d'admin >>");
        if (prig.dialogs.size() == d0 + 1) {
            auto& dlg = *prig.dialogs.back();
            const std::string d = drawMenu(dlg);
            check(has(d, "Refuser 2 demandes d'admin") && find<ui::Button>(*dlg.widgetRoot(), "demandes-admins.refus.ok")->text() == "Refuser les 2", "le titre ; << Refuser les 2 >>");
            check(type(*dlg.widgetRoot(), "demandes-admins.refus.raison", "doublon") && click(*dlg.widgetRoot(), "demandes-admins.refus.ok") && lastOk(prig),
                  "les 2 refusees : " + lastText(prig));
        }
        std::string s1, s2;
        check(nsf.ws->adminRequestState(s1).ok && s1 == "refusee" && n2f.ws->adminRequestState(s2).ok && s2 == "refusee", "les deux PC de Nina voient << refusee >>");
        check(wf.ws->fetchJournal(nullptr).ok && journalCount(*wf.ws, "demande_refuser") == 3, "le journal : 3 lignes << demande_refuser >> en tout");
        const std::string t = draw(*page, 1400.f, 900.f);
        check(rowsOf(*page, "demandes-admins.table") == 0 && has(t, "Aucune demande en attente"), "plus rien en attente");
    }
    return finish();
}
