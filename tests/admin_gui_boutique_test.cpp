// =============================================================================
//  tests/admin_gui_boutique_test.cpp - 1.46.0 : LES PAGES << BOUTIQUE >> ET
//  << COMMANDES >> DE L'APPLICATION DE L'EDITEUR, et le dialogue << Brancher
//  Stripe... >> (license_admin/gui/PageShop.cpp), dessines A BLANC (sans SDL)
// -----------------------------------------------------------------------------
//  Sans le service (le reglage et les commandes adoptes comme apres un releve) :
//  l'etat de Stripe (mode test), la vente, le catalogue (chaque offre et chaque
//  option du registre, une case changee : le brouillon, un montant illisible
//  refuse, l'ecart entre le prix affiche et le montant), Enregistrer (un service
//  qui ne repond pas : refuse, c'est dit) ; un service 1.44.0 : << deploie la
//  1.46.0 pour la boutique >> et le bouton Deployer ; les commandes (le filtre, le
//  detail, Delivrer, Rembourser... : une question d'abord, Revoir...) ; le
//  tableau de bord (<< a livrer >>) ; le dialogue : la cle dans un champ masque,
//  son mode lu, le champ vide des le clic, jamais dans un message. Aucun reseau :
//  l'adresse de Stripe des essais est 127.0.0.1:9 (rien n'y repond).
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/license_admin/Shop.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace admintest;
namespace shop = license_admin::shop;

namespace {

std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}
std::string drawMenu(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1400.f, 900.f);
}
bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}
bool visibleBtn(ui::Widget& root, std::string_view id) {
    auto* b = root.findById(id);
    return b && b->visible();
}
int rowOf(const ui::ITableModel& m, std::size_t col, const std::string& text) {
    for (std::size_t i = 0; i < m.rowCount(); ++i)
        if (m.cellText(static_cast<ui::RowIndex>(i), col).find(text) != std::string::npos) return static_cast<int>(i);
    return -1;
}

shop::Order order(int n, const std::string& state, const std::string& company) {
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
    if (state != "a_payer") {
        o.stripePayment = "pi_essai" + std::to_string(n);
        o.paid = g_now - 3000 * n;
        o.receiptUrl = "https://invoice.stripe.invalid/i/in_essai" + std::to_string(n);
    }
    return o;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_boutique_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    shop::setStripeApiBaseForTests("http://127.0.0.1:9");   // jamais le vrai Stripe : rien ne repond ici
    Fixture fx(source, "boutique-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    HostRig rig(fx);
    gui::Workspace& ws = *fx.ws;
    {   // un prix affiche pour l'offre PROFESSIONAL (l'ecart du catalogue)
        licensing::Registry reg = ws.registry();
        for (auto& e : reg.editions)
            if (e.id == "PROFESSIONAL") e.price = "490 \xE2\x82\xAC HT / an";
        check(ws.signRegistry(reg).ok, "le registre : un prix affiche pour PROFESSIONAL");
        (void)ws.takeServiceJob();
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Boutique : sans service\n");
    auto bp = gui::makeShopPage(rig.host());
    {
        const std::string t = draw(*bp);
        check(has(t, "Boutique") && has(t, "Stripe") && has(t, "Vente en ligne") && has(t, "Catalogue"), "la page : Stripe, la vente, le catalogue");
        check(has(t, "Le service des licences n'est pas r\xC3\xA9gl\xC3\xA9"), "sans service : c'est dit");
        check(!enabledBtn(*bp, "boutique.brancher") && !enabledBtn(*bp, "boutique.enregistrer"), "Brancher Stripe, Enregistrer : grises sans service");
        auto* table = find<ui::TableView>(*bp, "boutique.catalogue");
        check(table && table->model() && rowOf(*table->model(), 0, "PROFESSIONAL - 1 an") >= 0 && rowOf(*table->model(), 0, "1 mois") >= 0 &&
                  rowOf(*table->model(), 0, "EVALUATION") < 0 && rowOf(*table->model(), 0, "FREE") < 0,
              "le catalogue : les offres du registre (ni l'evaluation, ni l'offre sans licence) et chaque duree des options");
    }

    std::printf("-- Boutique : le reglage releve (adopte), le catalogue change\n");
    check(ws.serviceConfigure("http://127.0.0.1:9").ok, "le service regle (rien n'y repond)");
    ws.reload();
    {
        shop::Settings s;
        s.active = true;
        s.autoDeliver = true;
        s.open = true;
        s.number = 3;
        s.stripe.connected = true;
        s.stripe.webhook = true;
        s.stripe.mode = "test";
        s.stripe.lastEvent = g_now - 120;
        s.articles = {{"ed:PROFESSIONAL", "edition", "PROFESSIONAL", "", 0, 49000, "Licence PROFESSIONAL - 1 an"}};
        ws.adoptShop(s);
        bp->refresh();
        const std::string t = draw(*bp);
        check(has(t, "Stripe en mode test") && has(t, "aucune carte r\xC3\xA9" "elle"), "Stripe branche : << Stripe en mode test >>, paiement d'essai");
        check(has(t, "Webhook re\xC3\xA7u pour la derni\xC3\xA8re fois"), "le dernier evenement recu");
        check(has(t, "Ouverte : 1 article"), "la boutique ouverte : 1 article");
        check(has(t, "490,00") && has(t, "Licence PROFESSIONAL - 1 an"), "le montant encaisse et le libelle de la facture");
        check(has(t, "affich\xC3\xA9 490 \xE2\x82\xAC HT / an (588,00\xC2\xA0\xE2\x82\xAC TTC), encaiss\xC3\xA9 490,00"), "l'ecart entre le prix affiche (HT) et le montant (TTC) est dit");
        check(enabledBtn(*bp, "boutique.brancher") && has(t, "Rebrancher Stripe"), "l'admin+ : Rebrancher Stripe");
        check(!enabledBtn(*bp, "boutique.enregistrer"), "rien de change : Enregistrer grise");
        auto* table = find<ui::TableView>(*bp, "boutique.catalogue");
        auto& m = *table->model();
        const int opt = rowOf(m, 0, "1 mois");
        const int pro = rowOf(m, 0, "PROFESSIONAL - 1 an");
        check(opt >= 0 && m.editable(static_cast<ui::RowIndex>(opt), 1) && m.editable(static_cast<ui::RowIndex>(opt), 3) && !m.editable(static_cast<ui::RowIndex>(opt), 2),
              "En vente, Montant, Libelle se changent ; le prix affiche non (le registre)");
        check(m.setCellText(static_cast<ui::RowIndex>(opt), 1, "Oui") && m.cellText(static_cast<ui::RowIndex>(opt), 1) == "Oui", "une option mise en vente");
        check(m.setCellText(static_cast<ui::RowIndex>(opt), 3, "29") && m.cellText(static_cast<ui::RowIndex>(opt), 3).find("29,00") == 0, "son montant : 29,00 EUR");
        check(!m.setCellText(static_cast<ui::RowIndex>(opt), 3, "0,1"), "0,10 EUR : refuse (0,50 au moins)");
        std::string t2 = draw(*bp);
        check(has(t2, "Montant illisible ou hors limites"), "le refus est dit sous le catalogue");
        check(m.setCellText(static_cast<ui::RowIndex>(pro), 4, "Licence Pro - un an"), "un libelle change");
        t2 = draw(*bp);
        check(has(t2, "Changements pas encore enregistr\xC3\xA9s (2 article(s) en vente)") && visibleBtn(*bp, "boutique.annuler"), "le brouillon : 2 articles, pas encore enregistres");
        check(enabledBtn(*bp, "boutique.enregistrer"), "Enregistrer : permis");
        check(click(*bp, "boutique.enregistrer") && rig.last() && !rig.last()->ok, "Enregistrer : le service ne repond pas -> refuse, c'est dit (" + (rig.last() ? rig.last()->message : std::string()) + ")");
        check(click(*bp, "boutique.annuler"), "Annuler les changements");
        t2 = draw(*bp);
        check(!has(t2, "Changements pas encore") && m.cellText(static_cast<ui::RowIndex>(opt), 1) == "Non", "le brouillon revient au reglage releve");
        check(click(*bp, "boutique.brancher") && !rig.dialogs.empty(), "Brancher Stripe... : le dialogue s'ouvre");
        rig.dialogs.clear();
    }

    std::printf("-- un service 1.44.0 : deploie la 1.46.0 pour la boutique\n");
    {
        license_admin::online::ServiceState st;
        st.reached = true;
        st.version = "1.44.0";
        ws.adoptServiceState(st);
        check(gui::shopServiceNote(ws) == "Ton service en ligne est en 1.44.0 : d\xC3\xA9ploie la 1.46.0 pour la boutique.", "la phrase de la boutique");
        bp->refresh();
        std::string t = draw(*bp);
        check(has(t, "Ton service en ligne est en 1.44.0 : d\xC3\xA9ploie la 1.46.0 pour la boutique") && visibleBtn(*bp, "boutique.deployer"),
              "Boutique : la phrase et Deployer le service");
        check(!enabledBtn(*bp, "boutique.brancher"), "Brancher Stripe attend le service 1.46.0");
        check(click(*bp, "boutique.deployer") && !rig.dialogs.empty(), "Deployer le service : le dialogue");
        rig.dialogs.clear();
        st.version = "1.46.0";
        ws.adoptServiceState(st);
        bp->refresh();
        t = draw(*bp);
        check(!has(t, "d\xC3\xA9ploie la 1.46.0") && !visibleBtn(*bp, "boutique.deployer"), "un service 1.46.0 : plus rien a dire");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Commandes\n");
    {
        shop::OrdersView v;
        v.orders = {order(4, "a_revoir", "Atelier d'essai"), order(3, "payee", "Client d'essai"), order(2, "livree", "Bureau d'essai"), order(1, "a_payer", "Client d'essai")};
        v.orders[0].error = "Le montant pay\xC3\xA9 (1,00 \xE2\x82\xAC) n'est pas celui de la commande";
        v.orders[2].licence = "XPG-2026-00007";
        v.counter = 9;
        v.toDeliver = 1;
        ws.adoptOrders(v);
        auto op = gui::makeOrdersPage(rig.host());
        std::string t = draw(*op);
        check(has(t, "Commandes") && has(t, "4 commande(s) \xC2\xB7 1 \xC3\xA0 livrer") && has(t, "livraison automatique en marche"), "le tableau, ce qui est a livrer");
        check(has(t, "Client d'essai") && has(t, "490,00\xC2\xA0\xE2\x82\xAC (essai)") && has(t, "pay\xC3\xA9" "e, \xC3\xA0 livrer") && has(t, "livr\xC3\xA9" "e"),
              "les lignes : la societe, le montant (essai), l'etat");
        check(has(t, "Choisis une commande"), "rien de choisi : le detail le dit");
        auto* table = find<ui::TableView>(*op, "commandes.table");
        const auto pick = [&](int number) {
            for (std::size_t i = 0; i < table->model()->rowCount(); ++i)
                if (table->model()->cellText(static_cast<ui::RowIndex>(i), 0) == std::to_string(number)) {
                    table->selectModelRows({static_cast<ui::RowIndex>(i)});
                    return true;
                }
            return false;
        };
        check(pick(3), "choisir la n° 3 (payee)");
        t = draw(*op);
        check(has(t, "Commande n\xC2\xB0 3") && has(t, "camille@exemple.invalid") && has(t, "pi_essai3") && has(t, "paiement d'essai"),
              "le detail : le courriel, le paiement Stripe, << paiement d'essai >>");
        check(enabledBtn(*op, "commandes.delivrer") && enabledBtn(*op, "commandes.stripe") && enabledBtn(*op, "commandes.recu") && enabledBtn(*op, "commandes.revoir"),
              "payee : Delivrer maintenant, Voir dans Stripe, Ouvrir le recu, Revoir...");
        check(click(*op, "commandes.revoir") && !rig.dialogs.empty(), "Revoir... : une question (la raison)");
        if (!rig.dialogs.empty()) {
            const std::string q = drawMenu(*rig.dialogs.back());
            check(has(q, "Mettre la commande n\xC2\xB0 3 \xC3\xA0 revoir"), "la question de Revoir...");
        }
        rig.dialogs.clear();
        check(pick(2), "choisir la n° 2 (livree)");
        t = draw(*op);
        check(has(t, "XPG-2026-00007") && !enabledBtn(*op, "commandes.delivrer") && enabledBtn(*op, "commandes.rembourser"), "livree : sa licence ; Rembourser... permis (l'admin+)");
        check(click(*op, "commandes.rembourser") && !rig.dialogs.empty(), "Rembourser... : une question d'abord");
        if (!rig.dialogs.empty()) {
            const std::string q = drawMenu(*rig.dialogs.back());
            check(has(q, "Rembourser la commande n\xC2\xB0 2 ?") && has(q, "XPG-2026-00007 reste valable"), "la question dit que la licence n'est pas revoquee d'office");
        }
        rig.dialogs.clear();
        check(pick(4), "choisir la n° 4 (a revoir)");
        t = draw(*op);
        check(has(t, "n'est pas celui de la commande") && enabledBtn(*op, "commandes.delivrer"), "a revoir : la raison ; Delivrer maintenant (apres revue)");
        check(pick(1), "choisir la n° 1 (a payer)");
        check(!enabledBtn(*op, "commandes.delivrer") && !enabledBtn(*op, "commandes.rembourser"), "a payer : ni livrer ni rembourser");
        auto* filter = find<ui::DropDown>(*op, "commandes.filtre");
        filter->setSelectedIndex(1);
        filter->selectionChanged->emit(1);
        (void)draw(*op);
        check(table->model()->rowCount() == 1 && table->model()->cellText(0, 0) == "3", "le filtre << a livrer >> : la n° 3");
        // un service 1.44.0 : la meme phrase sur la page des commandes
        license_admin::online::ServiceState st;
        st.reached = true;
        st.version = "1.44.0";
        ws.adoptServiceState(st);
        op->refresh();
        t = draw(*op);
        check(has(t, "d\xC3\xA9ploie la 1.46.0 pour la boutique") && visibleBtn(*op, "commandes.deployer"), "Commandes : un service 1.44.0, c'est dit");
        st.version = "1.46.0";
        ws.adoptServiceState(st);
    }

    std::printf("-- le tableau de bord : a livrer\n");
    {
        ws.setOrdersToDeliver(2);
        auto dp = gui::makeDashboardPage(rig.host());
        const std::string t = draw(*dp);
        check(has(t, "2 commandes pay\xC3\xA9" "es \xC3\xA0 livrer"), "<< 2 commandes payees a livrer >>");
        ws.setOrdersToDeliver(0);
        dp->refresh();
        check(!has(draw(*dp), "\xC3\xA0 livrer"), "rien a livrer : rien n'est dit");
    }

    std::printf("-- Brancher Stripe... : la cle masquee, jamais gardee\n");
    {
        auto d = gui::makeStripeDialog(rig.host());
        (void)d->Initialize();
        std::string t = drawMenu(*d);
        auto* wm = dynamic_cast<menu::WidgetMenu*>(d.get());
        ui::Widget& root = *wm->widgetRoot();
        auto* key = find<ui::InputText>(root, "stripe.cle");
        check(has(t, "Brancher Stripe") && has(t, "Colle ta cl\xC3\xA9 secr\xC3\xA8te Stripe") && key && key->masked(), "le dialogue : la cle dans un champ masque");
        key->setText("pk_test_" + std::string(24, 'x'));
        t = drawMenu(*d);
        check(has(t, "C'est la cl\xC3\xA9 publiable"), "une cle publiable : dit tout de suite");
        const std::string secret = "sk_test_" + std::string(24, 'k');
        key->setText(secret);
        t = drawMenu(*d);
        check(has(t, "Cl\xC3\xA9 du mode test"), "le mode lu de la cle");
        const std::size_t before = rig.reports.size();
        check(click(root, "stripe.brancher"), "Brancher");
        t = drawMenu(*d);
        check(key->text().empty(), "le champ est vide des le clic");
        check(rig.reports.size() > before && !rig.reports.back().ok && has(rig.reports.back().message, "Stripe ne r\xC3\xA9pond pas"),
              "Stripe ne repond pas (l'adresse des essais) : refuse, c'est dit (" + (rig.reports.empty() ? std::string() : rig.reports.back().message) + ")");
        bool leaked = false;
        for (const auto& r : rig.reports)
            if (has(r.text(), secret)) leaked = true;
        for (const auto& f : rig.flashes)
            if (has(f, secret)) leaked = true;
        check(!leaked && !has(t, secret), "la cle n'est dans aucun message");
    }
    shop::setStripeApiBaseForTests({});
    return finish();
}
