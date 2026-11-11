// =============================================================================
//  tests/admin_gui_listes2_test.cpp - 1.47.0 (planches 06, 07, 12) : REVOCATIONS,
//  HISTORIQUE, VERSIONS - la barre, le clic droit, le depliage, dessines A BLANC
// -----------------------------------------------------------------------------
//  Sur un poste Admin d'essai (la cle d'essai << Camille Essai >>, trois licences
//  d'essai, un PC puis deux licences revoquees, une copie du journal des admins
//  signee par cette cle ; aucun reseau, aucun service) :
//  - Revocations : la barre (Raison, Par qui, Periode ; chercher), << Par >> lu
//    dans le journal, le clic droit dans l'ordre de la regle (Ouvrir la licence,
//    Deplier ; Aller a ; Filtrer sur ; Copier ; Exporter ; en rouge Retirer,
//    Revoquer toute la licence), << Filtrer sur > Cette raison >> pose la puce,
//    plusieurs lignes (ni Retirer ni Aller a), la ligne depliee, la selection
//    gardee quand la barre filtre, << Son historique >> : l'Historique filtre ;
//  - Historique : la barre (Type, Qui, Periode) a la place de la liste des genres,
//    << Qui >> lu dans le journal, le clic droit (rien en rouge, << Rien ne
//    s'efface >>), la ligne depliee (la meme licence), exporter 2 lignes ;
//  - Versions (une liste peinte) : un releve d'ESSAI donne a la page sans le
//    service (adoptVersionsView - un faux releve, dit comme tel), la barre (Sorte,
//    Importante, A jour), un clic deplie (les notes, les fichiers, les PC dessus),
//    le clic droit (Renvoyer..., Aller a, Filtrer sur, Copier, Exporter), le clavier ;
//  - la mise en page (1100 x 700, 1373 x 917) : la barre au-dessus de la liste.
//  XPG_ESSAI_COPIE=<dossier> : le poste d'essai y est copie (<dossier>/poste, les captures) ; avec
//  XPG_PHRASE_ESSAI (12 caracteres au moins) : la cle d'essai sauvegardee (<dossier>/cle.xpgcle) et retiree de la
//  copie - << XPG-License-Admin.exe restaurer-cle >> la remet sous Windows (Wine).
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"
#include "../src/ui/widgets/ListMenu.hpp"
#include "../src/ui/widgets/UnfoldCard.hpp"
#include "admin_gui_harness.hpp"

#include "../src/core/Json.hpp"
#include "../src/deploy/Crypto.hpp"
#include "../src/license_admin/KeyBackup.hpp"
#include "../src/license_admin/gui/ListKit.hpp"
#include "../src/licensing/Admins.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace license_admin::gui {
// PageVersions.cpp : les essais - la page montre ce releve comme s'il venait du service.
bool adoptVersionsView(ui::Widget& page, VersionsView view, std::vector<FileToResend> resend);
} // namespace license_admin::gui

using namespace admintest;
namespace adm = licensing::admins;
namespace json = core::json;
namespace dc = deploy::crypto;

namespace {

std::string draw(ui::Widget& w, float width = 1141.f, float height = 813.f) {
    TextRenderer r;
    std::string t = paint(w, r, width, height);
    if (std::getenv("XPG_ESSAI_MONTRER")) std::printf("----\n%s\n", t.c_str());
    return t;
}
void writeFile(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}

// Le menu du clic droit d'une table : ces lignes choisies, ouvert ; ses entrees.
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
std::string childLabels(const std::vector<ui::PopupMenu::Item>& items, const std::string& parent) {
    std::string s;
    for (const auto& it : items)
        if (it.label == parent)
            for (const auto& c : it.children) s += c.label + ";";
    return s;
}
// L'id d'une entree (la premiere qui commence ainsi) ; -1 : absente, grisee.
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
std::size_t rows(ui::Widget& page, std::string_view id) {
    auto* t = find<ui::TableView>(page, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}
int rowOf(ui::TableView& t, std::size_t col, const std::string& text) {
    for (std::size_t i = 0; t.model() && i < t.model()->rowCount(); ++i)
        if (t.model()->cellText(static_cast<ui::RowIndex>(i), col) == text) return static_cast<int>(i);
    return -1;
}
std::string cell(ui::TableView& t, int row, std::size_t col) {
    return row >= 0 && t.model() ? t.model()->cellText(static_cast<ui::RowIndex>(row), col) : std::string();
}
std::string cardText(ui::TableView& t, ui::RowIndex r) {
    auto* card = dynamic_cast<ui::UnfoldCard*>(t.unfoldWidget(r));
    return card ? card->text() : std::string();
}
std::size_t valueCount(const ui::FilterBar& bar, std::size_t field, const std::string& value) {
    for (const auto& [v, n] : bar.valueCounts(field))
        if (v == value) return n;
    return 0;
}
// Ces textes, dans cet ordre (chacun apres le precedent) ; vide : oui, sinon le premier qui manque.
std::string inOrder(const std::string& s, const std::vector<std::string>& parts) {
    std::size_t at = 0;
    for (const auto& p : parts) {
        const auto f = s.find(p, at);
        if (f == std::string::npos) return p;
        at = f + p.size();
    }
    return {};
}

// Une chaine du journal des admins, comme le service la garde (numero, precedent, recu, cle, entree, signature).
struct Chain {
    std::uint64_t            n{0};
    std::string              prev;
    std::vector<std::string> lines;
};
void addLine(Chain& c, const lic::SigningKey& key, const std::string& action, const std::string& object, const std::string& summary, lic::Seconds when) {
    adm::JournalEntry e;
    e.when = when;
    e.action = action;
    e.object = object;
    e.summary = summary;
    e.pc = "PC-ESSAI-01";
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

// Un releve d'ESSAI des versions (un faux : aucun service) - quatre versions, trois admins.
gui::PublishedVersion version(const std::string& v, lic::Seconds when, bool important, std::vector<std::string> notes, bool adminPresent = true) {
    gui::PublishedVersion p;
    p.version = v;
    p.published = when;
    p.important = important;
    p.notes = std::move(notes);
    gui::VersionFileState s, a;
    s.file = {"f-studios-" + v, "API-IHM-Studio-Setup-" + v + ".exe", std::string(64, 'a'), "studios", "tous", 57u * 1024u * 1024u};
    s.present = true;
    a.file = {"f-admin-" + v, "XPG-License-Admin-Setup-" + v + ".exe", "b1c2" + std::string(60, 'd'), "admin", "admins", 6900u * 1024u};
    a.present = adminPresent;
    p.files = {s, a};
    return p;
}

// Le point d'un texte dessine (le premier qui l'est tel quel) ; faux : pas dessine.
bool pointOf(ui::Widget& w, const std::string& text, gfx::Point& at, float width = 1141.f, float height = 813.f) {
    TextRenderer r;
    (void)paint(w, r, width, height);
    for (const auto& d : r.draws)
        if (d.text == text) {
            at = d.at;
            return true;
        }
    return false;
}
void mouse(ui::Widget& w, gfx::Point p, ui::MouseButton b, int clicks = 1) {
    (void)w.dispatch(ui::MouseDown{p, b, clicks, {}});
    (void)w.dispatch(ui::MouseUp{p, b, {}});
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_listes2_test <racine du depot>\n");
        return 2;
    }
    installMetrics();
    Fixture fx(argv[1], "listes2-gui");
    check(fx.ws->createKey("Camille Essai", true).ok && fx.ready(), "poste d'essai pret (la cle d'essai << Camille Essai >>, le registre)");
    HostRig rig(fx);
    gui::ShellHost h = rig.host();
    gui::Workspace& ws = *fx.ws;
    const lic::SigningKey* editor = ws.signingKey();
    if (!editor) return finish();
    const std::string idA = "XPG-2026-00021", idB = "XPG-2026-00022", idC = "XPG-2026-00023";
    const std::string plus = ws.key().name;   // l'admin+ : le nom de la cle d'essai (<< CLE D'ESSAI ... (Camille Essai) >>)
    const std::string reqA1 = fx.request("pcA1", "ATELIER-PC1"), reqA2 = fx.request("pcA2", "ATELIER-PC2"), reqB1 = fx.request("pcB1", "ESSAI-PC1"),
                      reqC1 = fx.request("pcC1", "BE-CALCUL-04");
    check(ws.recordRequests({reqA1, reqA2, reqB1, reqC1}).ok, "4 demandes not\xC3\xA9" "es (l'Historique)");
    g_now += 600;
    const lic::Seconds tA = g_now;
    check(fx.licence(idA, "Atelier Exemple", "PROFESSIONAL", {reqA1, reqA2}).ok, "la licence d'Atelier Exemple (2 PC)");
    g_now += 120;
    check(fx.licence(idB, "Soci\xC3\xA9t\xC3\xA9 d'essai", "PERSONAL", {reqB1}).ok, "la licence de la Soci\xC3\xA9t\xC3\xA9 d'essai");
    g_now += 120;
    check(fx.licence(idC, "Bureau d'\xC3\xA9tudes Exemple", "ENTERPRISE", {reqC1}).ok, "la licence du Bureau d'\xC3\xA9tudes Exemple");
    std::string pcKey, pcName;
    if (const auto* l = ws.licence(idA); l && !l->licence.devices.empty()) {
        pcKey = l->licence.devices.front().key;
        pcName = l->licence.devices.front().name;
    }
    check(!pcKey.empty() && !pcName.empty(), "le premier PC d'Atelier Exemple : " + pcName);
    g_now += 3600;
    check(ws.revoke(idA, pcKey, "PC remplac\xC3\xA9", false).ok, "un PC d'Atelier Exemple revoque");
    const lic::Seconds tR1 = g_now;
    g_now += 3 * 86400;
    check(ws.revoke(idB, "", "Fin de contrat", false).ok, "la licence de la Soci\xC3\xA9t\xC3\xA9 d'essai revoquee");
    const lic::Seconds tR2 = g_now;
    g_now += 60;
    check(ws.revoke(idC, "", "Fin de contrat", false).ok, "la licence du Bureau d'\xC3\xA9tudes revoquee");
    const lic::Seconds tR3 = g_now;
    (void)ws.takeServiceJob();
    // La copie du journal des admins (comme apres un releve) : la licence signee, puis les trois revocations, par Camille.
    Chain chain;
    addLine(chain, *editor, "licence_creer", idA, "a sign\xC3\xA9 la licence " + idA, tA);
    addLine(chain, *editor, "licence_revoquer", idA, "a r\xC3\xA9voqu\xC3\xA9 un PC de la licence " + idA, tR1);
    addLine(chain, *editor, "licence_revoquer", idB, "a r\xC3\xA9voqu\xC3\xA9 la licence " + idB, tR2);
    addLine(chain, *editor, "licence_revoquer", idC, "a r\xC3\xA9voqu\xC3\xA9 la licence " + idC, tR3);
    storeChain(fx.admin, chain);
    ws.reload();
    g_now += 3600;
    check(ws.role() == gui::Role::Plus && ws.journalLines().rows.size() == 4, "le journal : 4 lignes, l'admin+ (la cle de ce poste)");
    if (const char* out = std::getenv("XPG_ESSAI_COPIE")) {   // les captures : ce poste d'essai, copie
        std::error_code ec;
        const fs::path dst = fs::path(out) / "poste";
        fs::remove_all(dst, ec);
        fs::create_directories(dst, ec);
        fs::copy(fx.admin, dst, fs::copy_options::recursive, ec);
        std::printf("  (poste d'essai copie dans %s%s)\n", U(dst).c_str(), ec ? (" : " + ec.message()).c_str() : "");
        std::string backup, why;
        if (const char* phrase = std::getenv("XPG_PHRASE_ESSAI"); phrase && license_admin::acceptablePassphrase(phrase, nullptr)) {
            if (license_admin::writeKeyBackup(*editor, phrase, backup, &why)) {
                writeFile(fs::path(out) / "cle.xpgcle", backup);
                fs::remove(dst / license_admin::kPublisherKeyFile, ec);   // la cle de ce PC : restaurer-cle la remet sous Windows
                std::printf("  (la cle d'essai sauvegardee : %s)\n", U(fs::path(out) / "cle.xpgcle").c_str());
            } else {
                std::printf("  (la sauvegarde de la cle : %s)\n", why.c_str());
            }
        }
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- R\xC3\xA9vocations : la barre, Par, le clic droit, plusieurs lignes, une ligne d\xC3\xA9pli\xC3\xA9" "e\n");
    {
        auto page = gui::makeRevocationsPage(h);
        std::string t = draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "revocations.barre");
        auto* table = find<ui::TableView>(*page, "revocations.table");
        if (!bar || !table) return finish();
        check(rows(*page, "revocations.table") == 3 && bar->countText() == "3 sur 3", "la barre, la table : 3 r\xC3\xA9vocations, << 3 sur 3 >>");
        check(page->findById("revocations.barre.puce.raison") && page->findById("revocations.barre.puce.par-qui") && page->findById("revocations.barre.puce.periode") &&
                  find<ui::InputText>(*page, "revocations.recherche"),
              "les puces de la planche 12 : Raison, Par qui, P\xC3\xA9riode ; revocations.recherche");
        check(has(t, "Raison") && has(t, "Par qui") && has(t, "P\xC3\xA9riode") && has(t, "Mes filtres"), "elles se dessinent ; Mes filtres");
        check(bar->chipFields() == std::vector<std::size_t>{3, 4, 5}, "dans l'ordre de la planche : Raison, Par qui, P\xC3\xA9riode");
        const int ra = rowOf(*table, 0, idA), rb = rowOf(*table, 0, idB), rc = rowOf(*table, 0, idC);
        check(ra >= 0 && rb >= 0 && rc >= 0, "les trois lignes");
        check(cell(*table, ra, 4) == plus && cell(*table, rb, 4) == plus && has(plus, "Camille Essai") && has(cell(*table, ra, 2), pcName),
              "<< Par >> : lu dans le journal (Camille Essai) ; le PC nomm\xC3\xA9");
        check(valueCount(*bar, 3, "Fin de contrat") == 2 && valueCount(*bar, 3, "PC remplac\xC3\xA9") == 1 && valueCount(*bar, 4, plus) == 3,
              "les nombres des puces : Fin de contrat 2, PC remplac\xC3\xA9 1 ; Camille Essai 3");
        bar->setValues(3, {"Fin de contrat"});
        check(rows(*page, "revocations.table") == 2 && bar->countText() == "2 sur 3", "Raison : Fin de contrat - 2 sur 3");
        bar->clearAll();
        check(type(*page, "revocations.recherche", pcName) && rows(*page, "revocations.table") == 1, "chercher le nom du PC : 1 ligne");
        check(type(*page, "revocations.recherche", ""), "recherche vid\xC3\xA9" "e");
        // Le clic droit (planche 06) : l'ordre de la regle, les vraies actions de la page.
        const int r1 = rowOf(*table, 0, idA);
        auto items = rightClick(*table, {static_cast<ui::RowIndex>(r1)});
        const std::string order = topLabels(items);
        check(order == "Ouvrir la licence;D\xC3\xA9plier;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);Exporter la liste sign\xC3\xA9" "e (.xpgrev)\xE2\x80\xA6;|Retirer la r\xC3\xA9vocation\xE2\x80\xA6;"
                       "R\xC3\xA9voquer toute la licence\xE2\x80\xA6;",
              "l'ordre de la r\xC3\xA8gle : " + order);
        const auto* retire = itemOf(items, "Retirer la r\xC3\xA9vocation");
        const auto* whole = itemOf(items, "R\xC3\xA9voquer toute la licence");
        check(retire && retire->danger && retire->enabled && whole && whole->danger, "Retirer, R\xC3\xA9voquer toute la licence : en rouge, \xC3\xA0 part");
        const std::string goTo = childLabels(items, "Aller \xC3\xA0");
        check(inOrder(goTo, {"La licence " + idA, "Le client : Atelier Exemple", "Ses PC (Activations)", "R\xC3\xA9voqu\xC3\xA9" "e par " + plus, "Sa ligne du journal (n\xC2\xB0 2)",
                             "Son historique"})
                  .empty(),
              "Aller \xC3\xA0 : la licence, le client, ses PC, l'admin, la ligne du journal, son historique : " + goTo);
        const std::string filters = childLabels(items, "Filtrer sur");
        check(has(filters, "Cette raison : PC remplac\xC3\xA9") && has(filters, "R\xC3\xA9voqu\xC3\xA9" "e par : " + plus) && has(filters, "Ce jour : "), "Filtrer sur : " + filters);
        const std::string copies = childLabels(items, "Copier");
        check(has(copies, "La licence") && has(copies, "L'empreinte du PC") && has(copies, "La cl\xC3\xA9 du PC") && has(copies, "La ligne"), "Copier : " + copies);
        const int byWho = idOf(items, "Filtrer sur", "R\xC3\xA9voqu\xC3\xA9" "e par");
        if (byWho >= 0) table->contextAction->emit(byWho);
        check(byWho >= 0 && bar->fieldActive(4) && bar->chipText(4) == "Par qui : " + plus && rows(*page, "revocations.table") == 3,
              "Filtrer sur > R\xC3\xA9voqu\xC3\xA9" "e par : la puce << " + bar->chipText(4) + " >>");
        bar->clearAll();
        const int reason = idOf(items, "Filtrer sur", "Cette raison");
        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idA))});
        if (reason >= 0) table->contextAction->emit(reason);
        check(reason >= 0 && rows(*page, "revocations.table") == 1 && bar->state().picks.front().viaMenu, "Filtrer sur > Cette raison : 1 ligne, la puce pos\xC3\xA9" "e par le clic droit");
        bar->clearAll();
        // La selection reste quand la barre filtre (retrouvee par sa cle).
        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idC))});
        bar->setValues(3, {"Fin de contrat"});
        auto sel = table->selectedModelRows();
        check(sel.size() == 1 && cell(*table, static_cast<int>(sel.front()), 0) == idC && find<ui::Button>(*page, "revocations.retirer")->enabled(),
              "la ligne choisie le reste quand la barre filtre ; Retirer actif (une ligne)");
        bar->clearAll();
        // Plusieurs lignes : ce qui vaut pour toutes (ni Ouvrir, ni Aller a, ni Retirer).
        items = rightClick(*table, {0, 1});
        const std::string many = topLabels(items);
        check(many.rfind("2 R\xC3\x89VOCATIONS CHOISIES;", 0) == 0 && has(many, "D\xC3\xA9plier les 2") && has(many, "Exporter 2 lignes (CSV)") && !has(many, "Aller \xC3\xA0") &&
                  !has(many, "Retirer") && !has(many, "Ouvrir"),
              "deux lignes : " + many);
        check(!find<ui::Button>(*page, "revocations.retirer")->enabled(), "deux lignes : le bouton Retirer gris\xC3\xA9 (il vise une r\xC3\xA9vocation)");
        rig.picks.push_back(U(fx.root / "sortie" / "deux-revocations"));
        const int ex = idOf(items, "", "Exporter 2 lignes");
        if (ex >= 0) table->contextAction->emit(ex);
        const std::string csv = readFile(fx.root / "sortie" / "deux-revocations.csv");
        check(ex >= 0 && rig.last() && rig.last()->ok && csv.rfind("\xEF\xBB\xBF", 0) == 0 && has(csv, "Licence;Client;PC;Raison") && !has(csv, "|"),
              "Exporter 2 lignes : le CSV (BOM, les colonnes montr\xC3\xA9" "es, sans la cl\xC3\xA9 cach\xC3\xA9" "e)");
        // Retirer, par le clic droit : la confirmation (rien n'est retire sans elle).
        const std::size_t dialogs = rig.dialogs.size();
        items = rightClick(*table, {static_cast<ui::RowIndex>(rowOf(*table, 0, idB))});
        const int un = idOf(items, "", "Retirer la r\xC3\xA9vocation");
        if (un >= 0) table->contextAction->emit(un);
        check(un >= 0 && rig.dialogs.size() == dialogs + 1 && ws.revocations().revoked.size() == 3, "Retirer la r\xC3\xA9vocation\xE2\x80\xA6 : la confirmation s'ouvre, rien n'est retir\xC3\xA9");
        // Une ligne depliee (planche 07).
        const int ra2 = rowOf(*table, 0, idA);
        check(ra2 >= 0 && table->setUnfolded(static_cast<ui::RowIndex>(ra2), true), "d\xC3\xA9plier le PC r\xC3\xA9voqu\xC3\xA9 d'Atelier Exemple");
        const std::string card = cardText(*table, static_cast<ui::RowIndex>(ra2));
        check(has(card, "LA LICENCE") && has(card, idA) && has(card, "Atelier Exemple") && has(card, "CE QUI EST R\xC3\x89VOQU\xC3\x89") && has(card, pcName) &&
                  has(card, "raison : PC remplac\xC3\xA9") && has(card, "Camille Essai") && has(card, "SON HISTOIRE") && has(card, "a sign\xC3\xA9 la licence") &&
                  has(card, "Ouvrir la licence") && has(card, "Retirer la r\xC3\xA9vocation"),
              "la ligne d\xC3\xA9pli\xC3\xA9" "e : la licence, ce qui est r\xC3\xA9voqu\xC3\xA9 et par qui, son histoire, ses boutons");
        t = draw(*page);
        check(has(t, "CE QUI EST R\xC3\x89VOQU\xC3\x89") && find<ui::Button>(*page, "revocations.depliage.retirer") != nullptr, "dessin\xC3\xA9" "e sous la ligne ; ses boutons ont un id");
        bar->setValues(3, {"PC remplac\xC3\xA9"});
        check(table->isUnfolded(0) && rows(*page, "revocations.table") == 1, "filtr\xC3\xA9" "e : la ligne d\xC3\xA9pli\xC3\xA9" "e le reste");
        bar->clearAll();
        // Entree / le double-clic : la licence.
        table->activated->emit(static_cast<ui::RowIndex>(rowOf(*table, 0, idB)));
        check(!rig.navigations.empty() && rig.navigations.back() == "licences" && gui::takePageFocus("licences") == idB, "Entr\xC3\xA9" "e : la licence (Licences, elle choisie)");
        // << Son historique >> : l'Historique, filtre sur la licence.
        items = rightClick(*table, {static_cast<ui::RowIndex>(rowOf(*table, 0, idC))});
        const int story = idOf(items, "Aller \xC3\xA0", "Son historique");
        if (story >= 0) table->contextAction->emit(story);
        check(story >= 0 && rig.navigations.back() == "historique", "Aller \xC3\xA0 > Son historique : la page Historique");
        auto hist = gui::makeHistoryPage(h);
        (void)draw(*hist);
        auto* hbar = find<ui::FilterBar>(*hist, "historique");
        check(hbar && rows(*hist, "historique.table") == 2 && hbar->chipText(2) == "Licence : " + idC, "l'Historique filtr\xC3\xA9 sur la licence (sa licence, sa r\xC3\xA9vocation)");
        // Une autre page demande une licence : la barre filtree sur elle.
        gui::requestPageFocus("revocations", "licence:" + idB);
        page->refresh();
        check(rows(*page, "revocations.table") == 1 && cell(*table, 0, 0) == idB, "<< licence:\xE2\x80\xA6 >> d'une autre page : la barre filtr\xC3\xA9" "e sur elle");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Historique : la barre (Type, Qui, P\xC3\xA9riode), le clic droit, une ligne d\xC3\xA9pli\xC3\xA9" "e\n");
    {
        auto page = gui::makeHistoryPage(h);
        std::string t = draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "historique");
        auto* table = find<ui::TableView>(*page, "historique.table");
        if (!bar || !table) return finish();
        // demandes A1, A2, B1, C1 ; licences A (2 PC), B, C ; revocations : A (un PC), B, C (les repetitions masquees)
        const std::size_t n = rows(*page, "historique.table");
        check(n == 11 && bar->countText() == "11 sur 11", "11 lignes : " + std::to_string(n));
        check(!find<ui::DropDown>(*page, "historique.type") && find<ui::InputText>(*page, "historique.recherche") && page->findById("historique.puce.type") &&
                  page->findById("historique.puce.qui") && page->findById("historique.puce.periode"),
              "la barre remplace la liste des genres : les puces Type, Qui, P\xC3\xA9riode ; historique.recherche reste");
        check(has(t, "r\xC3\xA9vocations r\xC3\xA9p\xC3\xA9t\xC3\xA9" "es masqu\xC3\xA9" "es") || has(t, "r\xC3\xA9vocation r\xC3\xA9p\xC3\xA9t\xC3\xA9" "e masqu\xC3\xA9" "e"), "les r\xC3\xA9p\xC3\xA9titions masqu\xC3\xA9" "es sont dites");
        check(valueCount(*bar, 1, "R\xC3\xA9vocation") == 3 && valueCount(*bar, 1, "Demande") == 4 && valueCount(*bar, 1, "Licence") == 4,
              "Type : 3 r\xC3\xA9vocations, 4 demandes, 4 licences");
        check(valueCount(*bar, 6, plus) == 5, "Qui : l'admin+ - les 2 lignes de la licence sign\xC3\xA9" "e, les 3 r\xC3\xA9vocations (le journal)");
        bar->setValues(1, {"R\xC3\xA9vocation"});
        check(rows(*page, "historique.table") == 3 && has(draw(*page), "3 lignes sur 11"), "Type : R\xC3\xA9vocation - 3 lignes sur 11");
        bar->clearAll();
        int rr = -1;
        for (std::size_t i = 0; i < rows(*page, "historique.table"); ++i)
            if (cell(*table, static_cast<int>(i), 1) == "R\xC3\xA9vocation" && cell(*table, static_cast<int>(i), 2) == idB) rr = static_cast<int>(i);
        check(rr >= 0, "la r\xC3\xA9vocation de la Soci\xC3\xA9t\xC3\xA9 d'essai");
        auto items = rightClick(*table, {static_cast<ui::RowIndex>(rr)});
        const std::string order = topLabels(items);
        check(order == "Ouvrir la licence;D\xC3\xA9plier;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);|Supprimer\xE2\x80\xA6;",   // 1.48.1 : la, grise (sa raison)
              "l'ordre de la r\xC3\xA8gle, rien en rouge : " + order);
        const std::string goTo = childLabels(items, "Aller \xC3\xA0");
        check(inOrder(goTo, {"La licence " + idB, "Le client : Soci\xC3\xA9t\xC3\xA9 d'essai", "Ses PC (Activations)", "La r\xC3\xA9vocation (R\xC3\xA9vocations)", "Par " + plus}).empty(),
              "Aller \xC3\xA0 : " + goTo);
        const int byType = idOf(items, "Filtrer sur", "Ce type");
        if (byType >= 0) table->contextAction->emit(byType);
        check(byType >= 0 && rows(*page, "historique.table") == 3 && bar->chipText(1) == "Type : R\xC3\xA9vocation", "Filtrer sur > Ce type : la puce << " + bar->chipText(1) + " >>");
        bar->clearAll();
        // La ligne depliee.
        int rl = -1;
        for (std::size_t i = 0; i < rows(*page, "historique.table"); ++i)
            if (cell(*table, static_cast<int>(i), 1) == "Licence" && cell(*table, static_cast<int>(i), 2) == idA) rl = static_cast<int>(i);
        check(rl >= 0 && table->setUnfolded(static_cast<ui::RowIndex>(rl), true), "d\xC3\xA9plier une ligne de la licence d'Atelier Exemple");
        const std::string card = cardText(*table, static_cast<ui::RowIndex>(rl));
        check(has(card, "LA LIGNE") && has(card, "sign\xC3\xA9" "e : " + idA) && has(card, "Atelier Exemple") && has(card, plus) && has(card, "LA M\xC3\x8AME LICENCE") &&
                  has(card, "R\xC3\xA9vocation") && has(card, "Ouvrir la licence") && has(card, "Ses PC"),
              "la ligne d\xC3\xA9pli\xC3\xA9" "e : ce qu'elle dit, qui (le journal), la m\xC3\xAAme licence, ses boutons");
        // Exporter 2 lignes (le CSV de l'historique).
        items = rightClick(*table, {0, 1});
        check(topLabels(items).rfind("2 LIGNES CHOISIES;", 0) == 0, "deux lignes : << 2 LIGNES CHOISIES >>");
        rig.picks.push_back(U(fx.root / "sortie" / "deux-lignes"));
        const int ex = idOf(items, "", "Exporter 2 lignes");
        if (ex >= 0) table->contextAction->emit(ex);
        const std::string csv = readFile(fx.root / "sortie" / "deux-lignes.csv");
        std::size_t lines = 0;
        for (char ch : csv) lines += ch == '\n' ? 1 : 0;
        check(ex >= 0 && csv.rfind("\xEF\xBB\xBF" "Date;Type;Licence;Client;PC;Empreinte;D", 0) == 0 && lines == 3, "Exporter 2 lignes : l'en-t\xC3\xAAte et 2 lignes");
        // Un client : << client:... >> filtre la barre.
        gui::requestPageFocus("historique", "client:Atelier Exemple");
        page->refresh();
        check(rows(*page, "historique.table") == 3, "<< client:Atelier Exemple >> : ses 3 lignes (2 PC sign\xC3\xA9s, 1 r\xC3\xA9vocation ; ses demandes ne nomment pas le client)");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Versions (un relev\xC3\xA9 d'ESSAI, sans le service) : la barre, un clic d\xC3\xA9plie, le clic droit, le clavier\n");
    {
        auto vp = gui::makeVersionsPage(h);
        (void)draw(*vp);
        auto* bar = find<ui::FilterBar>(*vp, "versions.barre");
        if (!bar) return finish();
        check(vp->findById("versions.barre.puce.sorte") && vp->findById("versions.barre.puce.importante") && vp->findById("versions.barre.puce.a-jour") &&
                  find<ui::InputText>(*vp, "versions.recherche"),
              "les puces de la planche 12 : Sorte, Importante, \xC3\x80 jour ; versions.recherche");
        gui::VersionsView v;   // un FAUX releve (aucun service) : quatre versions, trois admins
        v.versions = {version("1.47.0", g_now - 6 * 86400, false, {"Le globe des PC.", "Les statistiques des admins.", "Filtrer et chercher partout."}),
                      version("1.46.2", g_now - 30 * 86400, true, {"Correctif : l'export PDF des grandes vues."}),
                      version("1.46.0", g_now - 60 * 86400, false, {"La boutique en ligne."}, false),
                      version("1.45.0", g_now - 120 * 86400, false, {})};
        v.byVersion = {{"1.47.0", 5}, {"1.46.2", 2}, {"1.45.0", 1}};
        v.bySoftware = {{"IHM-Studio", {{"1.47.0", 3}, {"1.46.2", 2}}}, {"XPG-License-Admin", {{"1.47.0", 2}, {"1.45.0", 1}}}};
        v.admins = {{"cle-camille", "Camille Essai", "XPG-License-Admin", "1.47.0", g_now - 60, true},
                    {"cle-lea", "L\xC3\xA9" "a Essai", "XPG-License-Admin", "1.47.0", g_now - 600, true},
                    {"cle-hugo", "Hugo Essai", "XPG-License-Admin", "1.45.0", g_now - 86400, false}};
        v.total = 8;
        v.upToDate = 5;
        gui::FileToResend again;
        again.file = v.versions[2].files[1].file;
        again.version = "1.46.0";
        check(gui::adoptVersionsView(*vp, v, {again}), "le relev\xC3\xA9 d'essai donn\xC3\xA9 \xC3\xA0 la page (un faux : pas de service)");
        std::string t = draw(*vp);
        check(has(t, "1.47.0") && has(t, "1.46.2") && has(t, "actuelle") && bar->countText() == "4 sur 4", "4 versions, << 4 sur 4 >>");
        check(valueCount(*bar, 2, "correctif") == 1 && valueCount(*bar, 2, "version") == 3 && valueCount(*bar, 3, "importante") == 1 &&
                  valueCount(*bar, 4, "actuelle") == 1 && valueCount(*bar, 4, "encore sur des PC") == 2 && valueCount(*bar, 4, "plus aucun PC") == 1,
              "Sorte (1 correctif, 3 versions), Importante (1), \xC3\x80 jour (actuelle, encore sur des PC 2, plus aucun PC 1)");
        bar->setValues(4, {"plus aucun PC"});
        t = draw(*vp);
        check(bar->countText() == "1 sur 4" && has(t, "1.46.0") && !has(t, "1.46.2"), "\xC3\x80 jour : plus aucun PC - la 1.46.0 seule");
        bar->clearAll();
        check(type(*vp, "versions.recherche", "boutique") && bar->countText() == "1 sur 4", "chercher dans les notes : << boutique >> - 1 sur 4");
        check(type(*vp, "versions.recherche", ""), "recherche vid\xC3\xA9" "e");
        // Un clic : la ligne choisie et depliee (les notes, les fichiers, les PC dessus).
        gfx::Point at{};
        check(pointOf(*vp, "1.47.0", at), "la ligne de la 1.47.0 se dessine");
        mouse(*vp, {at.x + 4.f, at.y + 4.f}, ui::MouseButton::Left);
        auto* card = find<ui::UnfoldCard>(*vp, "versions.depliage");
        const std::string ct = card ? card->text() : std::string();
        check(has(ct, "LES NOTES") && has(ct, "Le globe des PC.") && has(ct, "LES FICHIERS") && has(ct, "API-IHM-Studio-Setup-1.47.0.exe") && has(ct, "LES PC DESSUS") &&
                  has(ct, "IHM-Studio : 3 PC") && has(ct, "Hugo Essai") && has(ct, "Les admins"),
              "un clic : la 1.47.0 d\xC3\xA9pli\xC3\xA9" "e - les notes, les fichiers, les PC dessus (Hugo pas \xC3\xA0 jour)");
        t = draw(*vp);
        check(has(t, "LES NOTES") && has(t, "LES PC DESSUS"), "la carte se dessine sous la ligne");
        mouse(*vp, {at.x + 4.f, at.y + 4.f}, ui::MouseButton::Left);
        check(find<ui::UnfoldCard>(*vp, "versions.depliage") == nullptr, "un second clic : repli\xC3\xA9" "e");
        // Le clic droit sur la 1.46.0 (son installateur de l'Admin a renvoyer).
        check(pointOf(*vp, "1.46.0", at), "la ligne de la 1.46.0");
        mouse(*vp, {at.x + 4.f, at.y + 4.f}, ui::MouseButton::Right);
        auto* menu = find<ui::PopupMenu>(*vp, "versions.menu");
        const auto items = menu ? menu->items() : std::vector<ui::PopupMenu::Item>{};
        const std::string order = topLabels(items);
        check(menu && menu->isOpen() &&
                  order == "D\xC3\xA9plier;|Renvoyer XPG-License-Admin-Setup-1.46.0.exe\xE2\x80\xA6;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);|Supprimer\xE2\x80\xA6;",   // 1.48.1 : la, grise (sa raison)
              "le clic droit, l'ordre de la r\xC3\xA8gle : " + order);
        check(has(childLabels(items, "Filtrer sur"), "Cette sorte : version") && has(childLabels(items, "Filtrer sur"), "\xC3\x80 jour : plus aucun PC") &&
                  has(childLabels(items, "Copier"), "Le SHA-256 de l'Admin") && has(childLabels(items, "Copier"), "Les notes"),
              "Filtrer sur, Copier : " + childLabels(items, "Filtrer sur") + " / " + childLabels(items, "Copier"));
        const int fs1 = idOf(items, "Filtrer sur", "\xC3\x80 jour");
        if (menu && fs1 >= 0) menu->itemChosen->emit(fs1);
        check(fs1 >= 0 && bar->countText() == "1 sur 4" && bar->chipText(4) == "\xC3\x80 jour : plus aucun PC", "Filtrer sur > \xC3\x80 jour : la puce << " + bar->chipText(4) + " >>");
        bar->clearAll();
        // Exporter la ligne (le CSV des versions).
        mouse(*vp, {at.x + 4.f, at.y + 4.f}, ui::MouseButton::Right);
        rig.picks.push_back(U(fx.root / "sortie" / "une-version"));
        const auto items2 = menu ? menu->items() : std::vector<ui::PopupMenu::Item>{};
        const int ex = idOf(items2, "", "Exporter la ligne");
        if (menu && ex >= 0) menu->itemChosen->emit(ex);
        const std::string csv = readFile(fx.root / "sortie" / "une-version.csv");
        check(ex >= 0 && csv.rfind("\xEF\xBB\xBF" "Version;Publi\xC3\xA9" "e;Sorte;Importante;\xC3\x80 jour;Fichiers;Notes\r\n1.46.0;", 0) == 0 && has(csv, "\xC3\xA0 renvoyer"),
              "Exporter la ligne : le CSV (BOM, ;) de la 1.46.0, son fichier \xC3\xA0 renvoyer");
        // Le clavier : bas (la ligne suivante), -> deplie, <- replie (le menu ferme : il prend le clavier quand il est ouvert).
        if (menu) menu->close();
        (void)vp->dispatch(ui::KeyDown{ui::Key::Down, {}, false});
        (void)vp->dispatch(ui::KeyDown{ui::Key::Right, {}, false});
        card = find<ui::UnfoldCard>(*vp, "versions.depliage");
        check(card && has(card->text(), "LES FICHIERS") && !has(card->text(), "Le globe des PC."), "bas puis -> : la version suivante d\xC3\xA9pli\xC3\xA9" "e");
        (void)vp->dispatch(ui::KeyDown{ui::Key::Left, {}, false});
        check(find<ui::UnfoldCard>(*vp, "versions.depliage") == nullptr, "<- : repli\xC3\xA9" "e");
        // Le panneau << Publier une version >> (1.46.0) ne change pas : les notes s'ecrivent.
        check(click(*vp, "versions.publier"), "Publier une version\xE2\x80\xA6 : le panneau");
        auto* notes = find<ui::MultiLineText>(*vp, "versions.notes");
        t = draw(*vp, 1141.f, 813.f);
        check(notes && !notes->readOnly() && has(t, "Versions publi\xC3\xA9" "es") && bar->bounds().y >= notes->bounds().bottom(),
              "le panneau ouvert : les notes modifiables, la barre sous le panneau");
        check(click(*vp, "versions.annuler"), "Annuler");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- La mise en page : la barre au-dessus de la liste (1100 x 700, 1373 x 917)\n");
    {
        auto rev = gui::makeRevocationsPage(h);
        auto hist = gui::makeHistoryPage(h);
        for (const auto& [w, hgt] : std::vector<std::pair<float, float>>{{1100.f - 232.f, 700.f - 104.f}, {1373.f - 232.f, 917.f - 104.f}}) {
            const std::string at = std::to_string(static_cast<int>(w)) + "x" + std::to_string(static_cast<int>(hgt));
            (void)draw(*rev, w, hgt);
            const gfx::Rect rb = find<ui::FilterBar>(*rev, "revocations.barre")->bounds(), rt = find<ui::TableView>(*rev, "revocations.table")->bounds();
            const gfx::Rect rbtn = find<ui::Button>(*rev, "revocations.exporter")->bounds();
            check(rb.y >= rbtn.bottom() && rb.bottom() <= rt.y && rb.right() <= w + 0.5f && rt.bottom() <= hgt + 0.5f, at + " R\xC3\xA9vocations : boutons, barre, table");
            (void)draw(*hist, w, hgt);
            const gfx::Rect hb = find<ui::FilterBar>(*hist, "historique")->bounds(), ht = find<ui::TableView>(*hist, "historique.table")->bounds();
            const gfx::Rect hbtn = find<ui::Button>(*hist, "historique.exporter")->bounds();
            check(hb.y >= hbtn.bottom() && hb.bottom() <= ht.y && hb.right() <= w + 0.5f && ht.bottom() <= hgt + 0.5f, at + " Historique : Exporter, barre, table");
        }
    }
    return finish();
}
