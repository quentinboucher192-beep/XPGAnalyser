// =============================================================================
//  tests/admin_gui_listes_test.cpp - 1.47.0 (planches 06, 07, 12) : LES LISTES DE
//  L'APPLICATION DE L'EDITEUR, dessinees A BLANC (sans SDL)
// -----------------------------------------------------------------------------
//  Sur un poste Admin d'essai (des licences, des PC, des commandes adoptees comme
//  apres un releve ; aucun reseau) :
//  - Commandes : la barre (ses puces, chercher), le clic droit dans l'ordre de la
//    regle, << Filtrer sur > Cet etat >> qui pose la puce, plusieurs lignes
//    (<< 2 COMMANDES CHOISIES >>, << Exporter 2 lignes (CSV) >> : le fichier), une
//    ligne depliee (le client et le paiement, la livraison) ;
//  - Licences : la barre, le clic droit (un droit, Aller a, Revoquer en rouge),
//    << Aller a > Ses PC >> : la page Activations filtree sur elle, la ligne
//    depliee (le resume, son histoire) ;
//  - Activations : la barre, << Filtrer sur > Ce client >>, la ligne depliee ;
//  - Clients : un client ouvert montre son resume, << Aller a > Ses licences >> :
//    la page Licences filtree sur lui ; << Le client >> d'une licence : le client
//    choisi et ouvert ;
//  - Demandes en ligne : la puce Etat d'office (En attente) ;
//  - la memoire des listes (une coquille ouverte : listes-filtres.txt) : un filtre
//    de << Mes filtres >> retrouve par une page neuve ; Ctrl+F.
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"
#include "../src/ui/widgets/ListMenu.hpp"
#include "../src/ui/widgets/UnfoldCard.hpp"
#include "admin_gui_harness.hpp"

#include "../src/license_admin/Shop.hpp"
#include "../src/license_admin/gui/ListKit.hpp"
#include "../src/license_admin/gui/Shell.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace admintest;
namespace shop = license_admin::shop;

namespace {

std::string draw(ui::Widget& w, float width = 1141.f, float height = 813.f) {
    TextRenderer r;
    std::string t = paint(w, r, width, height);
    if (std::getenv("XPG_ESSAI_MONTRER")) std::printf("----\n%s\n", t.c_str());
    return t;
}

shop::Order order(int n, const std::string& state, const std::string& company, const std::string& licence = {}) {
    shop::Order o;
    o.number = n;
    o.device = "0123456789abcdef0123456789abcdef";
    o.deviceKey = std::string(64, 'a');
    o.article = "ed:PROFESSIONAL";
    o.kind = "edition";
    o.edition = "PROFESSIONAL";
    o.amount = 49000;
    o.currency = "eur";
    o.label = "Licence PROFESSIONAL - 1 an";
    o.company = company;
    o.name = "Camille Essai";
    o.email = "camille@exemple.invalid";
    o.state = state;
    o.mode = "test";
    o.created = g_now - 3600 * n;
    o.updated = g_now - 60;
    o.stripeSession = "cs_test_essai" + std::to_string(n);
    o.licence = licence;
    if (state != "a_payer") {
        o.stripePayment = "pi_essai" + std::to_string(n);
        o.paid = g_now - 3000 * n;
        o.receiptUrl = "https://invoice.stripe.invalid/i/in_essai" + std::to_string(n);
    }
    return o;
}

// Le menu du clic droit d'une table : ces lignes choisies, ouvert ; ses entrees (un sous-menu : << Aller a > ... >>).
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
// L'id d'une entree (<< Filtrer sur >>, << Cet \xC3\xA9tat : >> : la premiere qui commence ainsi) ; -1 : absente, grisee.
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
std::string cardText(ui::TableView& t, ui::RowIndex r) {
    auto* card = dynamic_cast<ui::UnfoldCard*>(t.unfoldWidget(r));
    return card ? card->text() : std::string();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_listes_test <racine du depot>\n");
        return 2;
    }
    installMetrics();
    Fixture fx(argv[1], "listes-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    HostRig rig(fx);
    gui::ShellHost h = rig.host();
    gui::Workspace& ws = *fx.ws;
    const std::string idA = "XPG-2026-00011", idB = "XPG-2026-00012";
    check(fx.licence(idA, "Atelier Exemple", "PROFESSIONAL", {fx.request("pcA1", "ATELIER-PC1"), fx.request("pcA2", "ATELIER-PC2")}).ok, "la licence d'Atelier Exemple (2 PC)");
    check(fx.licence(idB, "Bureau d'\xC3\xA9tudes Exemple", "ENTERPRISE", {fx.request("pcB1", "BE-CALCUL-04")}).ok, "la licence du Bureau d'etudes Exemple");
    (void)ws.takeServiceJob();
    shop::OrdersView v;
    v.orders = {order(214, "payee", "Client d'essai"), order(212, "livree", "Bureau d'\xC3\xA9tudes Exemple", idB), order(211, "livree", "Atelier Exemple", idA),
                order(210, "a_payer", "Laboratoire d'essai")};
    v.counter = 4;
    v.toDeliver = 1;
    ws.adoptOrders(v);
    ws.reload();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Commandes : la barre, le clic droit, plusieurs lignes, une ligne depliee\n");
    {
        auto page = gui::makeOrdersPage(h);
        std::string t = draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "commandes");
        auto* table = find<ui::TableView>(*page, "commandes.table");
        check(bar && table && rows(*page, "commandes.table") == 4, "la barre, la table : 4 commandes");
        check(has(t, "P\xC3\xA9riode") && has(t, "Article") && has(t, "\xC3\x89tat") && has(t, "Livr\xC3\xA9" "e par") && has(t, "Filtre") && has(t, "Mes filtres") &&
                  bar && bar->countText() == "4 sur 4",
              "les puces de la planche 12 : Periode, Article, Etat, Livree par ; + Filtre, Mes filtres ; 4 sur 4");
        check(type(*page, "commandes.recherche", "bureau") && rows(*page, "commandes.table") == 1 && bar->countText() == "1 sur 4", "chercher << bureau >> : 1 sur 4");
        check(type(*page, "commandes.recherche", ""), "recherche videe");
        // Le clic droit : l'ordre de la regle.
        const int r212 = table ? rowOf(*table, 0, "212") : -1;
        auto items = table && r212 >= 0 ? rightClick(*table, {static_cast<ui::RowIndex>(r212)}) : std::vector<ui::PopupMenu::Item>{};
        const std::string order0 = topLabels(items);
        check(order0.rfind("D\xC3\xA9plier;|D\xC3\xA9livrer maintenant;Ouvrir le re\xC3\xA7u;Voir dans Stripe;Revoir\xE2\x80\xA6;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);|"
                                "Rembourser\xE2\x80\xA6;",
                                0) == 0,
              "l'ordre de la regle (planche 06) : " + order0);
        const auto* deliver = itemOf(items, "D\xC3\xA9livrer maintenant");
        check(deliver && !deliver->enabled && !deliver->note.empty(), "Delivrer maintenant grise sur une livree, avec la raison : " + (deliver ? deliver->note : std::string()));
        const auto* refund = itemOf(items, "Rembourser");
        check(refund && refund->danger, "Rembourser... : en rouge, a part");
        check(idOf(items, "Aller \xC3\xA0", "Sa licence " + idB) >= 0 && idOf(items, "Aller \xC3\xA0", "Le client : Bureau") >= 0, "Aller a : sa licence, le client");
        const int f = idOf(items, "Filtrer sur", "Cet \xC3\xA9tat");
        if (table && f >= 0) table->contextAction->emit(f);
        check(f >= 0 && rows(*page, "commandes.table") == 2 && bar->chipText(5) == "\xC3\x89tat : livr\xC3\xA9" "e" && bar->state().picks.front().viaMenu,
              "Filtrer sur > Cet etat : la puce << Etat : livree >> (posee par le clic droit), 2 lignes");
        t = draw(*page);
        check(has(t, "clic droit") && has(t, "Effacer les filtres"), "la puce dit << clic droit >> ; Effacer les filtres");
        bar->clearAll();
        // Plusieurs lignes : ce qui vaut pour toutes.
        items = table ? rightClick(*table, {0, 1}) : items;
        const std::string many = topLabels(items);
        check(many.rfind("2 COMMANDES CHOISIES;", 0) == 0 && has(many, "Copier les 2 lignes;Exporter 2 lignes (CSV);") && !has(many, "Aller \xC3\xA0") && !has(many, "Rembourser"),
              "deux lignes : << 2 COMMANDES CHOISIES >>, Copier les 2 lignes, Exporter 2 lignes ; ni Aller a ni Rembourser : " + many);
        rig.picks.push_back(U(fx.root / "sortie" / "deux-commandes"));
        const int ex = idOf(items, "", "Exporter 2 lignes");
        if (table && ex >= 0) table->contextAction->emit(ex);
        const std::string csv = readFile(fx.root / "sortie" / "deux-commandes.csv");
        check(ex >= 0 && rig.last() && rig.last()->ok && csv.rfind("\xEF\xBB\xBF", 0) == 0 && has(csv, "214") && has(csv, "212") && !has(csv, "210"),
              "Exporter 2 lignes : le CSV (BOM), seulement ces deux-la");
        // Une ligne depliee (planche 07).
        check(table && table->setUnfolded(static_cast<ui::RowIndex>(rowOf(*table, 0, "212")), true), "deplier la n\xC2\xB0 212");
        const std::string card = table ? cardText(*table, static_cast<ui::RowIndex>(rowOf(*table, 0, "212"))) : std::string();
        check(has(card, "LE CLIENT, LE PAIEMENT") && has(card, "Bureau d'\xC3\xA9tudes Exemple") && has(card, "LA LIVRAISON") && has(card, idB) && has(card, "Le re\xC3\xA7u") &&
                  has(card, "Stripe"),
              "la ligne depliee : le client et le paiement, la livraison (sa licence), Le recu, Stripe");
        t = draw(*page);
        check(has(t, "LA LIVRAISON") && find<ui::Button>(*page, "commandes.depliage.recu") != nullptr, "dessinee sous la ligne ; ses boutons ont un id");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Licences : la barre, le clic droit, Aller a > Ses PC (Activations filtree), une ligne depliee\n");
    {
        auto page = gui::makeLicencesPage(h);
        std::string t = draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "licences.barre");
        auto* table = find<ui::TableView>(*page, "licences.table");
        check(bar && table && rows(*page, "licences.table") == 2 && has(t, "Offre") && has(t, "Sign\xC3\xA9" "e par") && find<ui::InputText>(*page, "licences.recherche"),
              "la barre : Client, Offre, Etat, Signee par ; licences.recherche");
        const int ra = table ? rowOf(*table, 0, idA) : -1;
        auto items = ra >= 0 ? rightClick(*table, {static_cast<ui::RowIndex>(ra)}) : std::vector<ui::PopupMenu::Item>{};
        const std::string order0 = topLabels(items);
        check(order0.rfind("D\xC3\xA9plier;|Ajouter un PC\xE2\x80\xA6;", 0) == 0 && has(order0, "|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);Exporter le fichier de licence\xE2\x80\xA6;|"
                                                                                    "R\xC3\xA9voquer\xE2\x80\xA6;"),
              "l'ordre de la regle : " + order0);
        const auto* revoke = itemOf(items, "R\xC3\xA9voquer");
        check(revoke && revoke->danger && revoke->enabled, "Revoquer... : en rouge, a part");
        const int pcs = idOf(items, "Aller \xC3\xA0", "Ses PC");
        if (pcs >= 0) table->contextAction->emit(pcs);
        check(pcs >= 0 && !rig.navigations.empty() && rig.navigations.back() == "activations", "Aller a > Ses PC : la page Activations");
        auto act = gui::makeActivationsPage(h);   // elle prend la demande en arrivant
        (void)draw(*act);
        auto* abar = find<ui::FilterBar>(*act, "activations");
        check(abar && rows(*act, "activations.table") == 2 && abar->chipText(3) == "Licence : " + idA, "Activations : filtree sur la licence (ses 2 PC), la puce le dit");
        // Une ligne depliee.
        check(table->setUnfolded(static_cast<ui::RowIndex>(ra), true), "deplier la licence d'Atelier Exemple");
        const std::string card = cardText(*table, static_cast<ui::RowIndex>(ra));
        check(has(card, "LE R\xC3\x89SUM\xC3\x89") && has(card, "PROFESSIONAL") && has(card, "PC : 2 sur") && has(card, "SON HISTOIRE") && has(card, "Ajouter un PC"),
              "la ligne depliee : le resume, son histoire, Ajouter un PC");
        t = draw(*page);
        check(has(t, "LE R\xC3\x89SUM\xC3\x89") && has(t, "clic droit : le menu"), "dessinee sous la ligne ; << clic droit : le menu >>");
        // << Le client >> : la page Clients, lui choisi et ouvert (son resume).
        items = rightClick(*table, {static_cast<ui::RowIndex>(ra)});
        const int cl = idOf(items, "Aller \xC3\xA0", "Le client : Atelier");
        if (cl >= 0) table->contextAction->emit(cl);
        check(cl >= 0 && rig.navigations.back() == "clients", "Aller a > Le client : la page Clients");
        auto clients = gui::makeClientsPage(h);
        (void)draw(*clients);
        auto* ct = find<ui::TableView>(*clients, "clients.table");
        const int crow = ct ? rowOf(*ct, 0, "Atelier Exemple") : -1;
        check(crow >= 0 && ct->isUnfolded(static_cast<ui::RowIndex>(crow)) && has(cardText(*ct, static_cast<ui::RowIndex>(crow)), "active") &&
                  rows(*clients, "clients.table") > 2,
              "Clients : Atelier Exemple ouvert - son resume (depliage) puis ses licences");
        // Activations : Filtrer sur > Ce client.
        abar->clearAll();
        auto* atable = find<ui::TableView>(*act, "activations.table");
        const int pc = atable ? rowOf(*atable, 0, "BE-CALCUL-04") : -1;
        items = pc >= 0 ? rightClick(*atable, {static_cast<ui::RowIndex>(pc)}) : items;
        const int byClient = idOf(items, "Filtrer sur", "Ce client");
        if (byClient >= 0) atable->contextAction->emit(byClient);
        check(byClient >= 0 && rows(*act, "activations.table") == 1, "Activations : Filtrer sur > Ce client (1 PC)");
        check(atable->setUnfolded(0, true) && has(cardText(*atable, 0), idB), "Activations : la ligne depliee dit sa licence");
        // Clients : Aller a > Ses licences -> Licences filtree sur lui.
        items = rightClick(*ct, {static_cast<ui::RowIndex>(crow)});
        const std::string corder = topLabels(items);
        check(corder.rfind("Sa fiche et ses courbes;Replier;|Nouvelle licence pour ce client\xE2\x80\xA6;|Aller \xC3\xA0;Filtrer sur;|Copier;Exporter la ligne (CSV);|"
                           "Supprimer le client\xE2\x80\xA6;",
                           0) == 0,
              "Clients : l'ordre de la regle : " + corder);
        const int sheet = idOf(items, "", "Sa fiche et ses courbes");
        if (sheet >= 0) ct->contextAction->emit(sheet);
        check(sheet >= 0 && !rig.navigations.empty() && rig.navigations.back() == "client" && itemOf(items, "Sa fiche")->shortcut.empty(),
              "Clients : Sa fiche et ses courbes (la sous-page du client ; Entree deplie ici)");
        items = rightClick(*ct, {static_cast<ui::RowIndex>(crow)});
        const auto* del = itemOf(items, "Supprimer le client");
        check(del && !del->enabled && !del->note.empty(), "Supprimer le client grise (une licence en cours), la raison dessous");
        const int sl = idOf(items, "Aller \xC3\xA0", "Ses licences");
        if (sl >= 0) ct->contextAction->emit(sl);
        page->refresh();
        check(sl >= 0 && rows(*page, "licences.table") == 1 && bar->chipText(1) == "Client : Atelier Exemple", "Aller a > Ses licences : Licences filtree sur Atelier Exemple");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Demandes en ligne : la puce Etat d'office\n");
    {
        auto page = gui::makeRequestsPage(h);
        const std::string t = draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "demandes");
        check(bar && bar->chipText(9) == "\xC3\x89tat : En attente" && has(t, "Logiciel") && has(t, "R\xC3\xA9pondue par"), "la puce Etat (En attente) d'office ; Logiciel, Repondue par");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la memoire des listes (la coquille) : Mes filtres, ce qui est choisi ; Ctrl+F\n");
    {
        const std::string file = gui::listkit::memoryFile(U(fx.admin));
        {
            gui::Shell shell(U(fx.admin), [] { return g_now; });   // elle branche la memoire (listes-filtres.txt)
            auto page = gui::makeOrdersPage(h);
            (void)draw(*page);
            auto* bar = find<ui::FilterBar>(*page, "commandes");
            if (bar) bar->setValues(5, {"pay\xC3\xA9" "e, \xC3\xA0 livrer"});
            check(bar && bar->saveAs("Commandes \xC3\xA0 livrer") && rows(*page, "commandes.table") == 1, "Mes filtres : << Commandes a livrer >> enregistre (1 ligne)");
            check(has(readFile(file), "commandes.mes=") && has(readFile(file), "commandes="), "listes-filtres.txt : ce qui est choisi et Mes filtres");
            auto again = gui::makeOrdersPage(h);
            (void)draw(*again);
            auto* bar2 = find<ui::FilterBar>(*again, "commandes");
            check(bar2 && bar2->saved().size() == 1 && bar2->currentSavedName() == "Commandes \xC3\xA0 livrer" && rows(*again, "commandes.table") == 1,
                  "une page neuve : le filtre retrouve, choisi (1 ligne)");
            (void)again->dispatch(ui::InputEvent{ui::KeyDown{ui::Key::F, {true, false, false, false}, false}});
            check(bar2 && bar2->searchField().focused(), "Ctrl+F : la recherche de la liste a le clavier");
            bar2->clearAll();
        }
        auto page = gui::makeOrdersPage(h);   // la coquille fermee : plus de memoire
        (void)draw(*page);
        auto* bar = find<ui::FilterBar>(*page, "commandes");
        check(bar && bar->saved().empty() && rows(*page, "commandes.table") == 4, "sans coquille : rien de relu (la memoire est celle de l'application)");
    }

    return finish();
}
