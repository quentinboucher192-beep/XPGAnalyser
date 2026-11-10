// =============================================================================
//  tests/admin_gui_admins_test.cpp - 1.44.0 : PLUSIEURS ADMINS A L'ECRAN
//  (pages Administrateurs, Droits, Journal des admins ; la coquille : le menu
//  selon le role, l'alerte en direct) - dessinees a blanc (TextRenderer,
//  HostRig), puis contre le VRAI service local (tests/service_local.hpp ; sans
//  Node ni wrangler : SAUTE pour cette partie).
// -----------------------------------------------------------------------------
//  Sans reseau :
//   - sans cle : le premier ecran offre les deux chemins (la cle de l'editeur,
//     devenir admin) ; en attente : l'empreinte en 6 groupes ;
//   - l'admin+ : une liste des admins signee ici (Lea, Hugo, Paul retire), le
//     tableau, la fiche, << Voir son journal >> ; la page Droits (un profil
//     modifie, enregistre, duplique, supprime ; les differences d'un admin
//     marquees) ; le journal (une chaine signee ici : le tri par colonne, les
//     filtres, une ligne depliee, le clic droit, une chaine rompue en rouge, la
//     criticite des actions) ; la coquille : le toast d'une alerte, << Voir >> ;
//   - un admin (Lea) : la page en lecture seule, ses droits, le menu sans le
//     journal, un bouton grise qui dit pourquoi.
//  Le service local : Nina demande a devenir admin (la page), l'admin+ la voit,
//  ouvre << Verifier et accepter... >>, coche l'empreinte, accepte ; Nina devient
//  admin ; puis l'admin+ la retire (la confirmation).
//  Des noms d'essai seulement (Lea, Hugo, Paul, Nina, << Atelier d'essai >>).
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"   // 1.47.0 : la barre de filtres du journal
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

void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
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
bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}
bool lastOk(const HostRig& rig) { return rig.last() && rig.last()->ok; }
std::string lastText(const HostRig& rig) { return rig.last() ? rig.last()->text() : std::string("(rien)"); }
bool closed(menu::IMenu& m) { return m.widgetRoot() && !m.widgetRoot()->visible(); }

bool selectRow(ui::Widget& root, std::string_view id, std::size_t col, const std::string& text) {
    auto* t = find<ui::TableView>(root, id);
    if (!t || !t->model()) return false;
    for (std::size_t r = 0; r < t->model()->rowCount(); ++r)
        if (t->model()->cellText(static_cast<ui::RowIndex>(r), col) == text) {
            t->selectModelRows({static_cast<ui::RowIndex>(r)}, true);
            return true;
        }
    return false;
}
bool pickValue(ui::Widget& root, std::string_view id, const std::string& value) {
    auto* d = find<ui::DropDown>(root, id);
    if (!d) return false;
    for (std::size_t i = 0; i < d->items().size(); ++i)
        if (d->items()[i].value == value) {
            d->setSelectedIndex(static_cast<int>(i));
            return true;
        }
    return false;
}
bool setSwitch(ui::Widget& root, std::string_view id, bool on) {
    auto* s = find<ui::ToggleButton>(root, id);
    if (!s || !s->enabled()) return false;
    s->setChecked(on);
    return true;
}
bool setCheck(ui::Widget& root, std::string_view id, bool on) {
    auto* c = find<ui::Checkbox>(root, id);
    if (!c || !c->enabled()) return false;
    c->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    return true;
}
// 1.47.0 : le nombre de lignes d'une valeur dans le menu d'une puce de la barre (la puce Admin du journal) ; 0 : absente.
std::size_t valueCount(const ui::FilterBar& bar, std::size_t field, const std::string& value) {
    for (const auto& [v, n] : bar.valueCounts(field))
        if (v == value) return n;
    return 0;
}

// La liste des admins signee par la cle de l'editeur (genre << admins >>).
std::string signedList(const lic::SigningKey& editor, const adm::AdminList& list) {
    return lic::writeBlock(lic::signBlock(lic::DocKind::Admins, adm::writeAdminList(list), editor.publicKey(),
                                          [&editor](std::string_view m) { return editor.sign(m); }));
}
void pinEditor(const fs::path& folder, const lic::SigningKey& editor) {
    writeFile(folder / "cles-editeur.json", "{\"format\":\"xpg-cles-editeur/1\",\"cles\":[\"" + editor.publicKeyHex() + "\"]}\n");
}

// Une chaine du journal des admins, comme le service la garde (numero, precedent, recu, cle, entree, signature).
struct Chain {
    std::uint64_t            n{0};
    std::string              prev;
    std::vector<std::string> lines;
};
void addLine(Chain& c, const lic::SigningKey& key, const std::string& action, const std::string& object, const std::string& summary, lic::Seconds when,
             const std::string& pc, const std::string& detail = {}, const std::string& before = {}, const std::string& after = {}) {
    adm::JournalEntry e;
    e.when = when;
    e.action = action;
    e.object = object;
    e.summary = summary;
    e.detail = detail;
    e.before = before;
    e.after = after;
    e.pc = pc;
    e.software = "XPG-License-Admin 1.44.0";
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

// Toute la pile (la coquille et un dialogue ouvert), comme la fenetre.
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

const std::string kLea = "L\xC3\xA9" "a";
// L'empreinte : les deux premiers groupes (la colonne << Sa cle >>), le premier groupe.
std::string kit_short(const std::string& fp) { return fp.substr(0, 9); }
std::string groups0(const std::string& fp) { return fp.substr(0, 4); }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_admins_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- sans cle : le premier ecran (les deux chemins)\n");
    {
        Fixture none(source, "admins-sans-cle");
        check(none.ws->role() == gui::Role::None, "aucune cle : aucun role");
        HostRig rig(none);
        auto page = gui::makeAdminsPage(rig.host());
        const std::string t = draw(*page);
        check(has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9 de l'\xC3\xA9" "diteur") && has(t, "Demander \xC3\xA0 devenir admin"), "les deux chemins : la cle de l'editeur, devenir admin");
        check(shown(*page, "admins.demande.nom") && shown(*page, "admins.demande.courriel") && shown(*page, "admins.demande.envoyer"), "le nom, le courriel, Envoyer");
        check(!enabledBtn(*page, "admins.demande.envoyer") && shown(*page, "admins.regler-service") && has(t, "R\xC3\xA9gler le service"),
              "sans l'adresse du service : Envoyer grise, Regler le service");
        check(!shown(*page, "admins.ajouter") && !shown(*page, "admins.table"), "ni Ajouter un admin, ni le tableau");
        check(click(*page, "admins.creer-cle") && !rig.navigations.empty() && rig.navigations.back() == "securite", "Creer la cle de l'editeur -> Parametres de securite");
        auto dash = gui::makeDashboardPage(rig.host());
        const std::string d = draw(*dash);
        check(has(d, "Demander \xC3\xA0 devenir admin") && has(d, "Cr\xC3\xA9" "er la cl\xC3\xA9"), "le tableau de bord : les deux chemins aussi");
        check(click(*dash, "tableau.devenir-admin") && rig.navigations.back() == "admins", "Demander a devenir admin... -> Administrateurs");
    }

    std::printf("-- en attente : l'empreinte a lire au telephone\n");
    {
        Fixture pf(source, "admins-attente");
        std::string why;
        auto nina = la::createAdminKey(U(pf.admin), "Nina", &why);
        pf.ws->reload();
        check(nina && pf.ws->role() == gui::Role::Pending, "une cle d'admin pas dans la liste : en attente (" + why + ")");
        HostRig rig(pf);
        auto page = gui::makeAdminsPage(rig.host());
        const std::string t = draw(*page);
        const auto g = adm::fingerprint(pf.ws->adminKey().keyHex);
        check(has(t, "L'admin+ doit accepter ta cl\xC3\xA9") && has(t, "Lis-lui ton empreinte") && has(t, g.substr(0, 4)) && has(t, g.substr(25, 4)),
              "l'empreinte en 6 groupes (" + g + ")");
        check(has(t, "En attendant, l'Admin ne peut rien signer") && shown(*page, "admins.attente.verifier"), "rien ne signe ; Verifier maintenant");
        auto dash = gui::makeDashboardPage(rig.host());
        check(has(draw(*dash), "Voir mon empreinte"), "le tableau de bord : Voir mon empreinte");
        ShellRig shell(pf.admin);
        const std::string s = shell.paint();
        check(has(s, "Nina \xC2\xB7 en attente") && has(s, "En attente d'acceptation"), "la coquille : << Nina - en attente >>, la cle en attente");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- l'admin+ : la liste signee, le tableau, la fiche\n");
    Fixture fx(source, "admins-plus");
    check(fx.ready(), "le poste de l'admin+ (cle d'essai, registre)");
    Fixture lf(source, "admins-lea");   // le poste de Lea (sa cle d'admin y nait)
    std::string why;
    auto leaKey = la::createAdminKey(U(lf.admin), kLea, &why);
    auto hugoKey = la::createAdminKey(U(fx.root / "hugo"), "Hugo", &why);
    auto paulKey = la::createAdminKey(U(fx.root / "paul"), "Paul", &why);
    check(leaKey && hugoKey && paulKey, "les cles d'essai de Lea, Hugo, Paul (" + why + ")");
    if (!leaKey || !hugoKey || !paulKey || !fx.ws->signingKey()) return finish();
    const lic::SigningKey& editor = *fx.ws->signingKey();
    adm::AdminList list;
    list.number = 3;
    list.issued = g_now - 86400;
    const auto admin = [](const lic::SigningKey& k, const std::string& name, const std::string& pc, const std::string& profile, std::vector<std::string> rights,
                          lic::Seconds since, lic::Seconds retired) {
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
    };
    list.admins.push_back(admin(*leaKey, kLea, "PC-COMMERCIAL-02", "Admin LV2", {"licence_creer", "licence_prolonger", "demandes", "codes"}, g_now - 40 * 86400, 0));
    list.admins.push_back(admin(*hugoKey, "Hugo", "PORTABLE-HUGO", adm::kAllRightsProfile, adm::allRights(), g_now - 30 * 86400, 0));
    list.admins.push_back(admin(*paulKey, "Paul", "PC-ATELIER-04", "Admin LV2", {}, g_now - 200 * 86400, g_now - 20 * 86400));
    const std::string block = signedList(editor, list);
    writeFile(fx.admin / "admins.xpgadm", block);
    adm::Profile lv2;
    lv2.name = "Admin LV2";
    lv2.rights = {"licence_creer", "licence_prolonger", "demandes", "codes", "options"};
    check(fx.ws->saveProfile(lv2).ok, "le profil << Admin LV2 >>");
    fx.ws->reload();
    check(fx.ws->adminList() && fx.ws->adminList()->admins.size() == 3, "la liste des admins signee, lue sur ce poste");
    // Le journal : 5 lignes signees, chainees. 1.47.0 : la cle de l'editeur relue - reload() la refait, `editor` (prise
    // avant) pendrait (selon le tas : un dossier temporaire plus long, et la chaine ne se verifie plus).
    Chain chain;
    addLine(chain, *fx.ws->signingKey(), "admin_ajouter", kLea, "a ajout\xC3\xA9 l'admin " + kLea, g_now - 5 * 86400, "PC-BUREAU-01");
    addLine(chain, *leaKey, "licence_creer", "XPG-2026-00412", "a sign\xC3\xA9 la licence XPG-2026-00412", g_now - 3 * 3600, "PC-COMMERCIAL-02",
            "{\"client\":\"Atelier d'essai\",\"offre\":\"PROFESSIONAL 1 an\"}");
    addLine(chain, *hugoKey, "offres", "PROFESSIONAL", "a modifi\xC3\xA9 l'offre PROFESSIONAL (prix)", g_now - 2 * 3600, "PORTABLE-HUGO", {},
            "{\"prix\":\"490 \xE2\x82\xAC\"}", "{\"prix\":\"520 \xE2\x82\xAC\"}");
    addLine(chain, *leaKey, "client_supprimer", "Client d'essai 14", "a supprim\xC3\xA9 le client Client d'essai 14", g_now - 3600, "PC-COMMERCIAL-02");
    addLine(chain, *paulKey, "licence_prolonger", "XPG-2026-00148", "a prolong\xC3\xA9 la licence XPG-2026-00148", g_now - 10 * 86400, "PC-ATELIER-04");
    storeChain(fx.admin, chain);
    {
        const auto v = fx.ws->journalLines();
        check(v.rows.size() == 5 && v.problems.empty(), "la chaine d'essai se verifie (" + std::to_string(v.problems.size()) + " probleme(s))");
    }

    HostRig rig(fx);
    auto page = gui::makeAdminsPage(rig.host());
    rig.refreshers.push_back([&] {
        fx.ws->reload();
        page->refresh();
    });
    {
        const std::string t = draw(*page);
        auto* table = find<ui::TableView>(*page, "admins.table");
        check(table && table->model() && table->model()->rowCount() == 4, "le tableau : l'admin+, Lea, Hugo, Paul");
        // 1.47.0 : la vue d'ensemble des admins (planche 01) : le classement, une ligne depliee ; la fiche d'un admin.
        check(has(t, "Administrateurs") && has(t, "3 actifs") && has(t, "1 retir\xC3\xA9") && has(t, "compar\xC3\xA9 aux 30 jours d'avant"),
              "la ligne du titre : 3 actifs (l'admin+ compris), 1 retire, la periode");
        check(has(t, "Admin+") && has(t, kLea) && has(t, "Hugo") && has(t, "Paul"), "chaque admin, l'admin+ en tete");
        check(has(t, "Retir\xC3\xA9") && has(t, "Actif"), "Retire, Actif");
        check(has(t, "Ses droits") && has(t, "Licences sign\xC3\xA9" "es") && has(t, "Admin LV2"), "la ligne de Lea (la premiere) depliee : ses chiffres, ses droits");
        check(click(*page, "admins.fiche"), "Ouvrir sa fiche");
        const std::string f = draw(*page);
        check(has(f, kit_short(adm::fingerprint(leaKey->publicKeyHex()))) && has(f, "certifi\xC3\xA9" "e \xE2\x9C\x93"), "sa fiche : sa cle (les deux premiers groupes), certifiee");
        check(click(*page, "admins.fiche.retour"), "retour a la liste");
        (void)draw(*page);
        check(shown(*page, "admins.retirer") && !enabledBtn(*page, "admins.retirer"), "Retirer cet admin... : grise sans le service");
        check(shown(*page, "admins.ajouter") && !enabledBtn(*page, "admins.ajouter"), "Ajouter un admin... : grise sans le service");
        check(click(*page, "admins.voir-journal") && rig.navigations.back() == "journal", "Voir son journal -> le journal");
        check(gui::takePageFocus("journal") == "admin:" + leaKey->publicKeyHex(), "... filtre sur Lea");
        check(click(*page, "admins.droits") && rig.navigations.back() == "droits" && gui::takePageFocus("droits") == "admin:" + leaKey->publicKeyHex(),
              "Ses droits... -> Droits, Lea choisie");
        check(selectRow(*page, "admins.table", 0, "Paul"), "choisir Paul");
        const std::string p = draw(*page);
        check(has(p, "certificat r\xC3\xA9voqu\xC3\xA9") && !shown(*page, "admins.retirer"), "Paul : retire (certificat revoque), plus de Retirer");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- les droits : les profils, un admin\n");
    {
        auto rp = gui::makeRightsPage(rig.host());
        std::string t = draw(*rp);
        check(has(t, "Les droits des admins") && has(t, "Tous les droits") && has(t, "Admin LV2") && has(t, "QUI LA V\xC3\x89RIFIE"), "les profils, Qui la verifie");
        check(selectRow(*rp, "droits.profils", 0, "Admin LV2"), "choisir << Admin LV2 >>");
        t = draw(*rp);
        check(has(t, "Le profil \xC2\xAB Admin LV2 \xC2\xBB") && has(t, "5 / 13") && has(t, "Studios et service") && has(t, "l'Admin"), "ses 5 fonctions, qui les verifie");
        check(!enabledBtn(*rp, "droits.enregistrer") && setSwitch(*rp, "droits.profil.offres", true) && enabledBtn(*rp, "droits.enregistrer"),
              "une fonction de plus : Enregistrer le profil");
        check(click(*rp, "droits.enregistrer") && lastOk(rig), "le profil enregistre : " + lastText(rig));
        bool offres = false;
        for (const auto& p : fx.ws->profiles())
            if (p.name == "Admin LV2") offres = std::find(p.rights.begin(), p.rights.end(), "offres") != p.rights.end();
        check(offres, "<< offres >> dans le profil");
        check(pickValue(*rp, "droits.admin", leaKey->publicKeyHex()), "choisir Lea a droite");
        t = draw(*rp);
        check(has(t, "retir\xC3\xA9 pour " + kLea) && has(t, "Copi\xC3\xA9s de Admin LV2") && has(t, "2 ajustements"), "ses differences avec son profil : retire pour Lea");
        check(setSwitch(*rp, "droits.admin.courriels", true), "un droit de plus pour Lea");
        t = draw(*rp);
        check(has(t, "ajout\xC3\xA9 pour " + kLea) && has(t, "Critique"), "ajoute pour Lea ; Ecrit dans le journal (Critique)");
        check(!enabledBtn(*rp, "droits.publier"), "Signer et publier : grise sans le service");
        check(click(*rp, "droits.recopier"), "Recopier le profil");
        t = draw(*rp);
        check(!has(t, "retir\xC3\xA9 pour " + kLea) && !has(t, "ajout\xC3\xA9 pour " + kLea), "recopie : plus aucune difference");
        check(click(*rp, "droits.dupliquer") && lastOk(rig), "Dupliquer : " + lastText(rig));
        bool copy = false;
        for (const auto& p : fx.ws->profiles()) copy = copy || p.name == "Admin LV2 (copie)";
        check(copy, "<< Admin LV2 (copie) >>");
        const std::size_t d0 = rig.dialogs.size();
        check(click(*rp, "droits.supprimer") && rig.dialogs.size() == d0 + 1, "Supprimer le profil : une confirmation");
        if (rig.dialogs.size() == d0 + 1) check(click(*rig.dialogs.back()->widgetRoot(), "confirmer.ok") && lastOk(rig), "supprime : " + lastText(rig));
        bool still = false;
        for (const auto& p : fx.ws->profiles()) still = still || p.name == "Admin LV2 (copie)";
        check(!still, "la copie n'est plus la");
        check(selectRow(*rp, "droits.profils", 0, adm::kAllRightsProfile), "choisir << Tous les droits >>");
        (void)draw(*rp);
        auto* sw = find<ui::ToggleButton>(*rp, "droits.profil.offres");
        check(sw && !sw->enabled() && !enabledBtn(*rp, "droits.supprimer") && !enabledBtn(*rp, "droits.renommer"), "<< Tous les droits >> ne se modifie pas");
        check(enabledBtn(*rp, "droits.nouveau"), "Nouveau profil...");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le journal : le tri, les filtres, une ligne depliee, le clic droit, la chaine\n");
    {
        auto jp = gui::makeJournalPage(rig.host());
        std::string t = draw(*jp, 1280.f, 800.f);
        auto* table = find<ui::TableView>(*jp, "journal.table");
        const auto rows = [&] { return table && table->model() ? table->model()->rowCount() : 0; };
        const auto firstLine = [&] { return table && table->model() && table->model()->rowCount() ? table->model()->cellText(0, 0) : std::string(); };
        check(rows() == 5, "les 5 lignes (30 derniers jours)");
        check(has(t, "Journal des admins") && has(t, "Cha\xC3\xAEne v\xC3\xA9rifi\xC3\xA9" "e : 5 lignes, aucune manquante, aucune modifi\xC3\xA9" "e"), "la chaine verifiee");
        check(has(t, "a sign\xC3\xA9 la licence ") && has(t, "XPG-2026-00412") && has(t, "Atelier d'essai \xC2\xB7 PROFESSIONA"), "l'action, l'objet en gras, le detail");
        check(has(t, "Moyenne") && has(t, "Haute") && has(t, "Critique") && has(t, "Info"), "les criticites");
        check(has(t, "tri\xC3\xA9" "es par date, la plus r\xC3\xA9" "cente d'abord") && firstLine() == "n\xC2\xB0 4", "triees par date, la plus recente d'abord");
        check(click(*jp, "journal.tri.admin") && firstLine() == "n\xC2\xB0 1", "tri par admin (A a Z) : l'admin+ (<< essai application >>) d'abord");
        check(click(*jp, "journal.tri.admin") && firstLine() == "n\xC2\xB0 5", "un second clic inverse : Paul d'abord");
        check(click(*jp, "journal.tri.criticite") && firstLine() == "n\xC2\xB0 1", "tri par criticite : la plus haute d'abord (Critique)");
        check(click(*jp, "journal.tri.quand") && firstLine() == "n\xC2\xB0 4", "de nouveau par date");
        // 1.47.0 : la barre << Filtrer et chercher >> (planche 12) - la puce Admin, la criticite, la periode (30 j d'office).
        auto* bar = find<ui::FilterBar>(*jp, "journal.barre");
        check(bar && bar->chipText(0) == "P\xC3\xA9riode : 30 j" && bar->countText() == "5 sur 5" && has(t, "Admin") && has(t, "Criticit\xC3\xA9"),
              "la barre : la periode (30 j d'office), 5 sur 5, les puces Admin et Criticite");
        check(bar && valueCount(*bar, 1, kLea) == 2, "le menu de la puce Admin : Lea, 2 lignes");
        if (bar) bar->filterOn(1, kLea);
        check(rows() == 2, "la puce Admin : Lea -> ses 2 lignes");
        if (bar) bar->clearField(1);
        check(rows() == 5, "sa croix : 5");
        check(bar && valueCount(*bar, 5, "Info") >= 1, "la criticite : Info compte ses lignes");
        if (bar) bar->setValues(5, {"Moyenne", "Haute", "Critique"});
        check(rows() == 4, "Info decoche (Moyenne, Haute, Critique) : 4 lignes");
        if (bar) bar->clearField(5);
        check(rows() == 5, "Info recoche : 5");
        check(type(*jp, "journal.recherche", "XPG-2026-00412") && rows() == 1, "la recherche : 1 ligne");
        check(type(*jp, "journal.recherche", "") && rows() == 5, "recherche effacee");
        if (bar) bar->setPeriod(0, 7);
        check(rows() == 4, "7 derniers jours : 4 lignes (Paul il y a 10 jours)");
        if (bar) bar->setPeriod(0, 0);
        check(rows() == 5, "tout le journal : 5");
        check(selectRow(*jp, "journal.table", 0, "n\xC2\xB0 3"), "deplier la ligne n. 3 (le tableau des scripts)");
        t = draw(*jp, 1280.f, 800.f);
        check(has(t, "CE QUI CHANGE") && has(t, "490 \xE2\x82\xAC") && has(t, "520 \xE2\x82\xAC"), "l'avant, l'apres");
        check(has(t, "Sign\xC3\xA9" "e par la cl\xC3\xA9 de Hugo") && has(t, "certificat valide") && has(t, "Ligne n\xC2\xB0 3, cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la n\xC2\xB0 2"),
              "signee par la cle de Hugo, chainee a la n. 2");
        check(shown(*jp, "journal.voir") && find<ui::Button>(*jp, "journal.voir")->text() == "Voir l'offre" && shown(*jp, "journal.tout-de"), "Voir l'offre, Tout le journal de Hugo");
        check(shown(*jp, "journal.admin"), "la pastille de Hugo (sa fiche) dans la ligne depliee");
        check(click(*jp, "journal.tout-de") && rows() == 1 && bar && bar->chipText(1) == "Admin : Hugo", "Tout le journal de Hugo : sa ligne (la puce Admin)");
        if (bar) bar->clearField(1);
        (void)draw(*jp, 1280.f, 800.f);
        check(rows() == 5, "Tous");
        // Le clic droit sur la premiere ligne (la n. 4) : la criticite de cette ligne.
        auto* menu = find<ui::PopupMenu>(*jp, "journal.menu");
        (void)draw(*jp, 1280.f, 800.f);
        // 1.47.0 : la barre (une rangee) remplace les pastilles des admins et de la criticite (deux) : les lignes commencent plus haut.
        (void)jp->dispatch(ui::InputEvent{ui::MouseDown{{500.f, 205.f}, ui::MouseButton::Right, 1, {}}});
        check(menu && menu->isOpen(), "le clic droit ouvre le menu de la ligne");
        // 1.47.0 (planche 06) : le meme ordre que partout - Deplier ; la criticite de la ligne ; Aller a, Filtrer sur ; Copier,
        // Exporter ; << Rien ne s'efface du journal >>.
        bool note = false, typeMark = false;
        int critique = -1;
        std::string order;
        if (menu)
            for (const auto& it : menu->items()) {
                if (!it.separator) order += it.label + ";";
                note = note || has(it.label, "Rien ne s'efface du journal");
                for (const auto& ch : it.children) {
                    typeMark = typeMark || ch.note == "celle du type";
                    if (it.label == "Criticit\xC3\xA9 de cette ligne" && ch.label == "Critique") critique = ch.id;
                }
            }
        check(note && typeMark && critique >= 0, "<< Rien ne s'efface >>, la criticite de la ligne (<< celle du type >>)");
        check(order.rfind("D\xC3\xA9plier;Criticit\xC3\xA9 de cette ligne;Aller \xC3\xA0;Filtrer sur;Copier;Exporter la ligne (CSV);", 0) == 0, "l'ordre de la regle : " + order);
        if (menu && critique >= 0) menu->itemChosen->emit(critique);   // Critique
        bool critical = false;
        for (const auto& r : fx.ws->journalLines().rows)
            if (r.line.number == 4) critical = r.overridden && r.level == adm::Level::Critical;
        check(critical && lastOk(rig), "la ligne n. 4 passe en Critique (une note a cote) : " + lastText(rig));
        // La chaine rompue : une ligne modifiee sur ce poste se voit, en rouge.
        const std::string original = readFile(fx.admin / "journal-admins.jsonl");
        std::string tampered = original;
        const auto pos = tampered.find("Client d'essai 14");
        if (pos != std::string::npos) tampered.replace(pos, 17, "Client d'essai 15");
        writeFile(fx.admin / "journal-admins.jsonl", tampered);
        jp->refresh();
        t = draw(*jp, 1280.f, 800.f);
        check(has(t, "Cha\xC3\xAEne : ") && (has(t, "probl\xC3\xA8me") || has(t, "probl\xC3\xA8mes")) && !has(t, "aucune modifi\xC3\xA9" "e"), "une ligne modifiee : la chaine le dit");
        writeFile(fx.admin / "journal-admins.jsonl", original);
        jp->refresh();
        check(has(draw(*jp, 1280.f, 800.f), "aucune modifi\xC3\xA9" "e"), "la copie remise : verifiee");
        // La criticite des actions (planche 04).
        const std::size_t d0 = rig.dialogs.size();
        check(click(*jp, "journal.criticite") && rig.dialogs.size() == d0 + 1, "Criticite des actions... : le dialogue");
        if (rig.dialogs.size() == d0 + 1) {
            auto& dlg = *rig.dialogs.back();
            std::string d = drawMenu(dlg);
            check(has(d, "Criticit\xC3\xA9 des actions") && has(d, "Signer une licence") && has(d, "ME PR\xC3\x89VENIR") && has(d, "Aussi par courriel") &&
                      has(d, "\xC3\xA0 regarder tout de suite"),
                  "les types d'action, me prevenir, le courriel, les legendes");
            check(!shown(*dlg.widgetRoot(), "criticite.licence_creer.prevenir"), "Moyenne : pas de << Me prevenir >>");
            check(pickValue(*dlg.widgetRoot(), "criticite.licence_creer", "critique"), "Signer une licence : Critique");
            (void)drawMenu(dlg);
            check(setSwitch(*dlg.widgetRoot(), "criticite.licence_creer.prevenir", true), "... me prevenir tout de suite");
            check(click(*dlg.widgetRoot(), "criticite.enregistrer") && lastOk(rig) && closed(dlg), "Enregistrer : " + lastText(rig));
            const auto c = fx.ws->criticality();
            check(c.actions.at("licence_creer").level == adm::Level::Critical && c.actions.at("licence_creer").notify, "la criticite gardee sur ce poste");
        }
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la coquille de l'admin+ : le menu, le fil, l'alerte en direct\n");
    {
        ShellRig shell(fx.admin);
        std::string t = shell.paint();
        check(has(t, "\xC2\xB7 admin+") && has(t, "CLE D'ESSAI"), "la marque : << (le nom de la cle) - admin+ >>");
        check(shown(shell.root(), "nav.admins") && shown(shell.root(), "nav.journal") && shown(shell.root(), "nav.versions"), "Administrateurs, Journal des admins, Versions");
        shell.shell->navigate("droits");
        t = shell.paint();
        check(shell.shell->currentPageId() == "droits" && has(t, "S\xC3\xA9" "curit\xC3\xA9") && has(t, "Les droits des admins"), "Droits : le fil << Securite > Administrateurs > Droits >>");
        // Une ligne << Me prevenir tout de suite >> relevee : le toast, meme sur une autre page.
        addLine(chain, *leaKey, "licence_revoquer", "XPG-2026-00377", "a r\xC3\xA9voqu\xC3\xA9 XPG-2026-00377", g_now - 30, "PC-COMMERCIAL-02",
                "{\"client\":\"Brasserie d'essai\"}");
        storeChain(fx.admin, chain);
        shell.shell->navigate("licences");
        shell.shell->takeAlerts();
        t = shell.paint();
        check(shell.shell->toasts().size() == 1 && shell.shell->unseenAlerts() == 1, "une alerte, pas encore vue");
        check(has(t, "Critique : " + kLea + " a r\xC3\xA9voqu\xC3\xA9 XPG-2026-00377") && has(t, "\xC3\xA0 l'instant \xC2\xB7 PC-COMMERCIAL-02 \xC2\xB7 Brasserie d'essai"),
              "le toast : Critique, qui, quoi ; quand, le PC, le client");
        check(click(shell.root(), "shell.toast.voir") && shell.shell->currentPageId() == "journal", "Voir : le journal");
        t = shell.paint();
        check(shell.shell->toasts().empty() && shell.shell->unseenAlerts() == 0 && has(t, "Ligne n\xC2\xB0 6, cha\xC3\xAEn\xC3\xA9" "e \xC3\xA0 la n\xC2\xB0 5"),
              "la ligne depliee ; l'alerte vue");
        shell.shell->takeAlerts();
        check(shell.shell->toasts().empty(), "une alerte n'est dite qu'une fois");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- un admin (Lea) : la lecture seule, ses droits, ce qui est grise\n");
    {
        writeFile(lf.admin / "admins.xpgadm", block);
        pinEditor(lf.admin, *fx.ws->signingKey());   // 1.47.0 : relue (voir le journal plus haut)
        lf.ws->reload();
        check(lf.ws->role() == gui::Role::Admin && lf.ws->can("licence_creer") && !lf.ws->can("offres"), "Lea : admin, ses droits");
        HostRig lrig(lf);
        auto lp = gui::makeAdminsPage(lrig.host());
        const std::string t = draw(*lp);
        check(has(t, "lecture seule") && has(t, "(toi)") && has(t, "Tes droits") && has(t, "4 sur 13"), "lecture seule ; (toi) ; tes droits, 4 sur 13");
        check(!shown(*lp, "admins.ajouter") && !shown(*lp, "admins.retirer") && !shown(*lp, "admins.droits-page"), "ni Ajouter, ni Retirer, ni Droits");
        check(has(t, "Ni les cl\xC3\xA9s, ni le journal, ni les boutons"), "ce qu'un admin voit, dit");
        auto rv = gui::makeRevocationsPage(lrig.host());
        (void)draw(*rv);
        auto* revoke = find<ui::Button>(*rv, "revocations.revoquer");
        check(revoke && !revoke->enabled() && has(revoke->tooltip(), "R\xC3\xA9voquer une licence") && has(revoke->tooltip(), "admin+"),
              "Revoquer : grise, le bouton dit pourquoi (" + (revoke ? revoke->tooltip() : std::string()) + ")");
        ShellRig shell(lf.admin);
        const std::string s = shell.paint();
        check(has(s, kLea + " \xC2\xB7 admin") && has(s, "Admin : " + kLea), "la marque et la cle : Lea, admin");
        check(shown(shell.root(), "nav.admins") && !shown(shell.root(), "nav.journal") && !shown(shell.root(), "nav.versions"), "le menu : sans le journal ni les versions");
        shell.shell->navigate("journal");
        check(shell.shell->currentPageId() != "journal" && shell.shell->lastMessage() && has(shell.shell->lastMessage()->result.message, "admin+"),
              "le journal : refuse, et dit");
    }

    // ------------------------------------------------------------------------------------------------ le vrai service local
    std::printf("-- le service local : devenir admin, accepter, retirer\n");
    Fixture wf(source, "admins-service");
    check(wf.ready(), "un second poste de l'admin+ (sans la liste d'essai)");
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
        wf.ws->reload();
    }
    Fixture nf(source, "admins-nina");
    check(nf.ws->serviceConfigure(svc.url()).ok, "le poste de Nina : l'adresse du service");
    nf.ws->reload();
    HostRig nrig(nf);
    auto np = gui::makeAdminsPage(nrig.host());
    nrig.refreshers.push_back([&] {
        nf.ws->reload();
        np->refresh();
    });
    {
        (void)draw(*np);
        check(enabledBtn(*np, "admins.demande.envoyer"), "Nina : Envoyer ma demande (le service regle)");
        check(type(*np, "admins.demande.nom", "Nina Essai") && type(*np, "admins.demande.courriel", "nina@exemple.invalid"), "son nom, son courriel");
        check(click(*np, "admins.demande.envoyer") && lastOk(nrig), "la demande envoyee : " + lastText(nrig));
        const std::string t = draw(*np);
        check(nf.ws->role() == gui::Role::Pending && has(t, groups0(nf.ws->adminKey().fingerprint)), "en attente ; son empreinte");
    }
    HostRig prig(wf);
    auto pp = gui::makeAdminsPage(prig.host());
    prig.refreshers.push_back([&] {
        wf.ws->reload();
        pp->refresh();
    });
    {
        check(click(*pp, "admins.relever") && lastOk(prig), "Relever : " + lastText(prig));
        const std::string t = draw(*pp);
        check(has(t, "Nina Essai") && has(t, "En attente") && shown(*pp, "admins.verifier.1"), "la demande de Nina : en attente, Verifier et accepter...");
        const std::size_t d0 = prig.dialogs.size();
        check(click(*pp, "admins.verifier.1") && prig.dialogs.size() == d0 + 1, "Verifier et accepter... : le dialogue");
        if (prig.dialogs.size() == d0 + 1) {
            auto& dlg = *prig.dialogs.back();
            (void)drawMenu(dlg);   // le releve des demandes (a la premiere image)
            std::string d = drawMenu(dlg);
            const std::string fp = nf.ws->adminKey().fingerprint;
            check(has(d, "Ajouter un admin") && has(d, "Demande re\xC3\xA7ue \xC2\xB7 \xC3\xA0 v\xC3\xA9rifier") && has(d, groups0(fp)), "la demande recue, son empreinte");
            check(has(d, "Un seul caract\xC3\xA8re diff\xC3\xA8re ?") && has(d, "J'ai v\xC3\xA9rifi\xC3\xA9 l'empreinte avec Nina"), "l'avertissement ; la case");
            auto* accept = find<ui::Button>(*dlg.widgetRoot(), "admins.accepter");
            check(accept && !accept->enabled() && accept->text() == "Accepter Nina comme admin", "Accepter Nina comme admin : grise sans la case");
            check(setCheck(*dlg.widgetRoot(), "admins.empreinte.verifiee", true) && accept && accept->enabled(), "la case cochee : Accepter");
            check(click(*dlg.widgetRoot(), "admins.accepter") && lastOk(prig) && closed(dlg), "Nina acceptee : " + lastText(prig));
        }
    }
    {
        check(click(*np, "admins.attente.verifier") && lastOk(nrig), "Nina : Verifier maintenant : " + lastText(nrig));
        nf.ws->reload();
        np->refresh();
        const std::string t = draw(*np);
        check(nf.ws->role() == gui::Role::Admin && has(t, "lecture seule"), "Nina est admin : la page en lecture seule");
    }
    {
        check(wf.ws->fetchJournal(nullptr).ok, "l'admin+ releve le journal");
        auto jp = gui::makeJournalPage(prig.host());
        const std::string t = draw(*jp, 1280.f, 800.f);
        check(has(t, "a ajout\xC3\xA9 l'admin") || has(t, "Nina"), "le journal : l'admin ajoute");
        check(click(*pp, "admins.relever") && lastOk(prig), "Relever");
        check(selectRow(*pp, "admins.table", 0, "Nina Essai"), "choisir Nina");
        (void)draw(*pp);
        const std::size_t d0 = prig.dialogs.size();
        check(enabledBtn(*pp, "admins.retirer") && click(*pp, "admins.retirer") && prig.dialogs.size() == d0 + 1, "Retirer cet admin... : la confirmation");
        if (prig.dialogs.size() == d0 + 1) {
            auto& dlg = *prig.dialogs.back();
            const std::string d = drawMenu(dlg);
            auto* box = find<ui::Checkbox>(*dlg.widgetRoot(), "admins.retirer.licences");
            check(has(d, "R\xC3\xA9voquer aussi ses licences") && box && !box->isChecked(), "<< Revoquer aussi ses licences >> : non par defaut");
            check(click(*dlg.widgetRoot(), "admins.retirer.ok") && lastOk(prig) && closed(dlg), "retiree : " + lastText(prig));
        }
        wf.ws->reload();
        const adm::Admin* n = wf.ws->adminList() ? wf.ws->adminList()->byKey(nf.ws->adminKey().keyHex) : nullptr;
        check(n && !n->active(), "Nina : retiree dans la liste signee");
    }
    return finish();
}
