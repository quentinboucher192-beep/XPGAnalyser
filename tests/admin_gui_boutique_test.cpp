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
//  Les tailles de la fenetre du client (1373x917, 1400x900) et 1920x1080 : aucune
//  colonne hors de la vue (le catalogue : l'ecart ; les commandes : l'etat) ; un
//  service 1.44.0 : un seul avertissement, les interrupteurs grises ; Stripe deja
//  branche : << Rebrancher Stripe >> ; le nom du PC (de la demande scellee).
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"   // 1.47.0 : la barre de filtres des Commandes
#include "admin_gui_harness.hpp"

#include "../src/license_admin/Shop.hpp"
#include "../src/licensing/Documents.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/licensing/KeyFile.hpp"
#include "../src/licensing/Service.hpp"
#include "../src/licensing/SignedFile.hpp"
#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace admintest;
namespace shop = license_admin::shop;
namespace gw = licensing::gateway;
namespace sv = licensing::service;

namespace {

std::string draw(ui::Widget& w, float width = 1400.f, float height = 900.f) {
    TextRenderer r;
    std::string t = paint(w, r, width, height);
    if (std::getenv("XPG_ESSAI_MONTRER")) std::printf("----\n%s\n", t.c_str());   // pour voir ce qui est dessine
    return t;
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
// Les tailles de la page (la fenetre moins le menu de gauche, 232 px, la barre du haut, 64, et celle du bas, 40) :
// la fenetre du client (1373x917), 1400x900 et 1920x1080.
struct PageSize {
    const char* window;
    float       w, h;
};
constexpr PageSize kSizes[] = {{"1373x917", 1141.f, 813.f}, {"1400x900", 1168.f, 796.f}, {"1920x1080", 1688.f, 976.f}};

// Aucune colonne hors de la vue : les colonnes montrees tiennent dans la table (sa barre de defilement comprise) et
// `must` en est.
bool columnsFit(const ui::TableView& t, const std::string& must, std::string& why) {
    float sum = 0.f;
    bool  seen = false;
    for (const auto& c : t.columns()) {
        if (!c.visible) continue;
        sum += c.width;
        if (c.title == must) seen = true;
    }
    why = std::to_string(static_cast<int>(sum)) + " px de colonnes, " + std::to_string(static_cast<int>(t.bounds().w)) + " px de table";
    return seen && sum + 12.f <= t.bounds().w;
}
bool columnShown(const ui::TableView& t, const std::string& title) {
    for (const auto& c : t.columns())
        if (c.title == title) return c.visible;
    return false;
}
std::size_t count(const std::string& s, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t at = s.find(part); at != std::string::npos; at = s.find(part, at + part.size())) ++n;
    return n;
}
// Le texte dessine dans le cadre d'un widget (une tuile du tableau de bord).
std::vector<std::string> drawnIn(const TextRenderer& r, const ui::Widget& w) {
    std::vector<std::string> out;
    const gfx::Rect b = w.bounds();
    for (const auto& d : r.draws)
        if (b.contains(d.at)) out.push_back(d.text);
    return out;
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
        {
            auto* cat = find<ui::TableView>(*bp, "boutique.catalogue");
            const int pr = cat ? rowOf(*cat->model(), 0, "PROFESSIONAL - 1 an") : -1;
            check(pr >= 0 && cat->model()->cellText(static_cast<ui::RowIndex>(pr), 5) == "\xE2\x89\xA0 affich\xC3\xA9 (588,00\xC2\xA0\xE2\x82\xAC TTC)" &&
                      has(t, "\xE2\x89\xA0 affich\xC3\xA9 (588,00"),
                  "l'ecart entre le prix affiche (HT, 588 EUR TTC) et le montant (490 EUR TTC) est dit, court");
            // Aucune colonne hors de la vue (l'ecart compris), a chaque taille.
            for (const auto& sz : kSizes) {
                (void)draw(*bp, sz.w, sz.h);
                std::string why;
                check(cat && columnsFit(*cat, "\xC3\x89" "cart", why), std::string("le catalogue a ") + sz.window + " : toutes les colonnes en vue, l'ecart compris (" + why + ")");
            }
            (void)draw(*bp);
        }
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

    // 1.47.0 : l'ecart se compare au prix que le client voit MAINTENANT (une reduction en cours), comme les Studios.
    std::printf("-- Boutique : une reduction en cours (le prix du moment), puis finie\n");
    {
        licensing::Registry reg = ws.registry();
        for (auto& e : reg.editions)
            if (e.id == "PROFESSIONAL") {
                e.price = "490 \xE2\x82\xAC HT / an";
                e.promoPrice = "390 \xE2\x82\xAC HT / an";
                e.promoUntil = g_now + 46 * 86400;   // le 22/12/2026
            }
        check(ws.signRegistry(reg).ok, "le registre : PROFESSIONAL reduite (390 EUR HT au lieu de 490) jusqu'au 22/12/2026");
        (void)ws.takeServiceJob();
        shop::Settings s = ws.shop();
        for (auto& a : s.articles)
            if (a.id == "ed:PROFESSIONAL") a.amount = 46800;   // 390 EUR HT = 468,00 EUR TTC
        ws.adoptShop(s);
        bp->refresh();
        std::string t = draw(*bp);
        auto* cat = find<ui::TableView>(*bp, "boutique.catalogue");
        auto& m = *cat->model();
        const int pro = rowOf(m, 0, "PROFESSIONAL - 1 an");
        const auto R = static_cast<ui::RowIndex>(pro < 0 ? 0 : pro);
        check(pro >= 0 && m.cellText(R, 5).empty() && !has(t, "\xE2\x89\xA0 affich\xC3\xA9"), "la reduction en cours : 468,00 EUR TTC (= 390 HT) n'est pas un ecart (" + m.cellText(R, 5) + ")");
        check(m.cellText(R, 2) == "390 \xE2\x82\xAC HT / an (au lieu de 490)" && has(t, "390 \xE2\x82\xAC HT / an (au lieu de 490)"),
              "le prix affiche : le prix du moment (" + m.cellText(R, 2) + ")");
        for (const auto& sz : kSizes) {
            (void)draw(*bp, sz.w, sz.h);
            std::string why;
            check(columnsFit(*cat, "\xC3\x89" "cart", why), std::string("le catalogue (un prix reduit) a ") + sz.window + " : toutes les colonnes en vue (" + why + ")");
        }
        (void)draw(*bp);
        check(m.setCellText(R, 1, "Non") && m.setCellText(R, 1, "Oui") && m.cellText(R, 3).find("468,00") == 0,
              "remise en vente pendant la reduction : le montant propose, 468,00 EUR TTC (" + m.cellText(R, 3) + ")");
        check(click(*bp, "boutique.annuler"), "Annuler les changements");
        // la reduction finie (l'heure du poste apres le 22/12) : le prix normal, et l'ecart revient
        const lic::Seconds keep = g_now;
        g_now += 60 * 86400;
        bp->refresh();
        t = draw(*bp);
        check(m.cellText(R, 2) == "490 \xE2\x82\xAC HT / an" && !has(t, "au lieu de 490"), "la reduction finie : le prix normal (" + m.cellText(R, 2) + ")");
        check(m.cellText(R, 5) == "\xE2\x89\xA0 affich\xC3\xA9 (588,00\xC2\xA0\xE2\x82\xAC TTC)", "la reduction finie : 468,00 EUR TTC est un ecart au prix normal (" + m.cellText(R, 5) + ")");
        check(m.setCellText(R, 1, "Non") && m.setCellText(R, 1, "Oui") && m.cellText(R, 3).find("588,00") == 0,
              "remise en vente apres la reduction : 588,00 EUR TTC propose (" + m.cellText(R, 3) + ")");
        check(click(*bp, "boutique.annuler"), "Annuler les changements (la reduction finie)");
        g_now = keep;
        bp->refresh();
    }

    // 1.46.1 : le montant SUIT le prix affiche et sa reduction (<< 12,00 EUR TTC (reduit jusqu'au 22/12, puis 60,00 EUR) >>), un
    // montant tape est libre (vide : il suit de nouveau), Enregistrer quand le registre a change ; les produits chez Stripe.
    std::printf("-- 1.46.1 : le montant suit le registre (une reduction), le montant libre, les produits chez Stripe\n");
    {
        const lic::Seconds until = g_now + 42 * 86400;
        licensing::Registry reg = ws.registry();
        for (auto& e : reg.editions)
            if (e.id == "ENTERPRISE") {
                e.price = "50 \xE2\x82\xAC HT /par an";
                e.promoPrice = "10 \xE2\x82\xAC";
                e.promoUntil = until;
            }
        check(ws.signRegistry(reg).ok, "le registre : ENTERPRISE 50 EUR HT, reduite a 10 EUR");
        (void)ws.takeServiceJob();
        shop::Settings s = ws.shop();
        shop::Article a{"ed:ENTERPRISE", "edition", "ENTERPRISE", "", 0, 6000, "Licence ENTERPRISE - 1 an"};
        a.free = false;   // suit le registre ; le service ne sait pas encore la reduction
        s.articles.push_back(a);
        s.catalog.state = "ok";
        s.catalog.products = 2;
        ws.adoptShop(s);
        bp->refresh();
        std::string t = draw(*bp);
        auto* cat = find<ui::TableView>(*bp, "boutique.catalogue");
        auto& m = *cat->model();
        const int ent = rowOf(m, 0, "ENTERPRISE - 1 an");
        const auto R = static_cast<ui::RowIndex>(ent < 0 ? 0 : ent);
        const std::string d = lic::isoDate(until);
        const std::string reduced = "12,00\xC2\xA0\xE2\x82\xAC TTC (r\xC3\xA9" "duit jusqu'au " + d.substr(8, 2) + "/" + d.substr(5, 2) + ", puis 60,00\xC2\xA0\xE2\x82\xAC)";
        check(ent >= 0 && m.cellText(R, 3) == reduced, "le montant suit la reduction : " + m.cellText(R, 3));
        check(m.cellText(R, 2) == "10 \xE2\x82\xAC (au lieu de 50)" && m.cellText(R, 5).empty(), "le prix du moment, aucun ecart (" + m.cellText(R, 2) + " | " + m.cellText(R, 5) + ")");
        check(has(t, "Les montants suivent le registre") && enabledBtn(*bp, "boutique.enregistrer") && !visibleBtn(*bp, "boutique.annuler"),
              "le registre a change : a enregistrer (sans changement a la main : rien a annuler)");
        check(has(t, "Produits chez Stripe : 2 \xC3\xA0 jour"), "la carte Stripe : les produits chez Stripe");
        for (const auto& sz : kSizes) {
            (void)draw(*bp, sz.w, sz.h);
            std::string why;
            check(columnsFit(*cat, "\xC3\x89" "cart", why) || sz.w < 1150.f, std::string("le catalogue (un montant reduit) a ") + sz.window + " : l'ecart en vue (" + why + ")");
        }
        (void)draw(*bp);
        check(m.setCellText(R, 3, "15") && m.cellText(R, 3) == "15,00\xC2\xA0\xE2\x82\xAC TTC (montant libre)" && m.cellText(R, 5) == "\xE2\x89\xA0 affich\xC3\xA9 (12,00\xC2\xA0\xE2\x82\xAC TTC)",
              "un montant tape : libre, l'ecart au prix du moment (" + m.cellText(R, 3) + " | " + m.cellText(R, 5) + ")");
        check(m.setCellText(R, 3, "") && m.cellText(R, 3) == reduced, "vide : il suit de nouveau le registre");
        check(click(*bp, "boutique.annuler"), "Annuler les changements");
        // la fin de la reduction passee : le montant de base, sans rien faire
        const lic::Seconds keep = g_now;
        g_now = until + 3600;
        bp->refresh();
        (void)draw(*bp);
        check(m.cellText(R, 3) == "60,00\xC2\xA0\xE2\x82\xAC TTC" && m.cellText(R, 5).empty(), "la reduction finie : 60,00 EUR TTC (" + m.cellText(R, 3) + ")");
        g_now = keep;
        // Stripe refuse les produits (une cle restreinte) : le bandeau le dit, la vente marche quand meme
        s = ws.shop();
        s.catalog.state = "refus";
        s.catalog.message = "Stripe refuse (cr\xC3\xA9" "er le prix) : The provided key does not have the required permissions. \xE2\x80\x94 Ta cl\xC3\xA9 restreinte doit avoir "
                            "\xC2\xAB Products : \xC3\xA9" "criture \xC2\xBB et \xC2\xAB Prices : \xC3\xA9" "criture \xC2\xBB.";
        ws.adoptShop(s);
        bp->refresh();
        t = draw(*bp);
        check(has(t, "Tes produits ne se voient pas dans Stripe") && has(t, "Products : \xC3\xA9" "criture") && has(t, "la vente marche quand m\xC3\xAAme"),
              "Stripe refuse les produits : le bandeau dit pourquoi et que la vente marche");
        s.catalog = {};
        s.articles.pop_back();
        ws.adoptShop(s);
        bp->refresh();
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
        check(count(t, "1.46.0 pour la boutique") == 1 && !has(t, "Service : ") && !has(t, "Refus\xC3\xA9"), "un seul avertissement (pas de << Service : Refuse : >> en plus)");
        check(!has(t, "pas encore lu") && !has(t, "(Relever)") && has(t, "service 1.44.0"), "la carte de Stripe : ni << pas encore lu >>, ni << (Relever) >> ; la version du service");
        check(!has(t, "Rebrancher") && has(t, "Brancher Stripe\xE2\x80\xA6"), "le bouton ne dit pas un branchement d'avant (pas lu sur ce service)");
        {
            auto* sell = find<ui::ToggleButton>(*bp, "boutique.vendre");
            auto* alone = find<ui::ToggleButton>(*bp, "boutique.livrer-seul");
            check(sell && alone && !sell->enabled() && !alone->enabled() && !sell->checked() && !alone->checked(),
                  "Vendre en ligne, Livrer seul : grises et decoches (le reglage d'avant n'est pas montre)");
        }
        check(!enabledBtn(*bp, "boutique.brancher"), "Brancher Stripe attend le service 1.46.0");
        check(click(*bp, "boutique.deployer") && !rig.dialogs.empty(), "Deployer le service : le dialogue");
        rig.dialogs.clear();
        st.version = "1.46.0";
        ws.adoptServiceState(st);
        bp->refresh();
        t = draw(*bp);
        check(!has(t, "d\xC3\xA9ploie la 1.46.0") && !visibleBtn(*bp, "boutique.deployer"), "un service 1.46.0 : plus rien a dire");
        {
            auto* sell = find<ui::ToggleButton>(*bp, "boutique.vendre");
            check(sell && sell->enabled() && sell->checked(), "un service 1.46.0 : Vendre en ligne de nouveau permis, coche (le reglage releve)");
        }
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Commandes\n");
    {
        shop::OrdersView v;
        v.orders = {order(4, "a_revoir", "Atelier d'essai"), order(3, "payee", "Client d'essai"), order(2, "livree", "Bureau d'essai"), order(1, "a_payer", "Client d'essai")};
        v.orders[0].error = "Le montant pay\xC3\xA9 (1,00 \xE2\x82\xAC) n'est pas celui de la commande";
        v.orders[2].licence = "XPG-2026-00007";
        {   // la n° 3 : la demande scellee d'un vrai PC d'essai (son nom se lit dans le detail)
            std::error_code ec;
            fs::create_directories(fx.root / "pc-camille", ec);
            std::string why;
            auto pc = lic::SigningKey::create(U(fx.root / "pc-camille" / "identite-pc.cle"), lic::deviceKeySpec(), "Poste de Camille", false, &why);
            check(pc != nullptr && ws.signer() != nullptr, "la cle du PC d'essai (" + why + ")");
            if (pc && ws.signer()) {
                lic::ActivationRequest r;
                r.deviceKey = pc->publicKeyHex();
                r.deviceName = "Poste de Camille";
                r.software = "IHM Studio 1.46.0 (essai)";
                r.createdAt = g_now - 3 * 3600;
                r.nonce = "0123456789abcdef0123456789abcdef";
                gw::OnlineRequest o;
                o.request = lic::writeBlock(lic::signBlock(lic::DocKind::Request, lic::writeRequest(r), pc->publicKey(), [&pc](std::string_view m) { return pc->sign(m); }));
                o.customer = "Client d'essai";
                o.contact = "Camille Essai";
                o.email = "camille@exemple.invalid";
                v.orders[1].device = sv::deviceId(pc->publicKey());
                v.orders[1].deviceKey = pc->publicKeyHex();
                v.orders[1].keyId = ws.signer()->keyId();
                v.orders[1].sealed = gw::sealFor(ws.signer()->publicKey(), gw::writeOnlineRequest(o));
            }
        }
        v.counter = 9;
        v.toDeliver = 1;
        ws.adoptOrders(v);
        auto op = gui::makeOrdersPage(rig.host());
        std::string t = draw(*op);
        check(has(t, "Commandes") && (has(t, "pas de r\xC3\xA9ponse du service") || has(t, "Le service ne r\xC3\xA9pond pas")), "le releve en arrivant : le service ne repond pas, c'est dit");
        op->refresh();   // de nouveau (le releve ne repart pas avant 20 s) : ce qui a ete adopte
        t = draw(*op);
        check(has(t, "4 commande(s) \xC2\xB7 1 \xC3\xA0 livrer") || has(t, "pas de r\xC3\xA9ponse du service") || has(t, "Le service ne r\xC3\xA9pond pas"), "le tableau, ce qui est a livrer");
        check(has(t, "Client d'essai") && has(t, "490,00\xC2\xA0\xE2\x82\xAC") && !has(t, "\xE2\x82\xAC (essai)") && has(t, "pay\xC3\xA9" "e, \xC3\xA0 livrer") &&
                  has(t, "livr\xC3\xA9" "e"),
              "les lignes : la societe, le montant (sans << (essai) >> : le mode est dans le detail), l'etat");
        check(has(t, "Choisis une commande"), "rien de choisi : le detail le dit");
        auto* table = find<ui::TableView>(*op, "commandes.table");
        // Aucune colonne hors de la vue, l'etat toujours la ; le client (aussi dans le detail) cede le premier.
        for (const auto& sz : kSizes) {
            (void)draw(*op, sz.w, sz.h);
            std::string why;
            check(table && columnsFit(*table, "\xC3\x89tat", why), std::string("les commandes a ") + sz.window + " : toutes les colonnes en vue, l'etat compris (" + why + ")");
        }
        check(table && columnShown(*table, "Client") && columnShown(*table, "Soci\xC3\xA9t\xC3\xA9"), "1920x1080 : le client et la societe");
        {
            const auto& m = *table->model();
            const int r4 = rowOf(m, 0, "4");
            check(r4 >= 0 && m.cellText(static_cast<ui::RowIndex>(r4), 4) == "490,00\xC2\xA0\xE2\x82\xAC" && m.cellText(static_cast<ui::RowIndex>(r4), 5) == "\xC3\xA0 revoir",
                  "les cases : le montant seul, l'etat");
            const ui::CellStyle st = m.cellStyle(static_cast<ui::RowIndex>(r4), 5);
            check(st.keepFgSelected && (st.fg || st.fgTone != ui::Tone::None), "l'etat garde sa couleur dans la ligne choisie");
        }
        t = draw(*op);
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
        check(has(t, "Poste de Camille \xC2\xB7 " + v.orders[1].device.substr(0, 8)), "le detail : le nom du PC (de la demande scellee), puis son identifiant court");
        check(enabledBtn(*op, "commandes.delivrer") && enabledBtn(*op, "commandes.stripe") && enabledBtn(*op, "commandes.recu") && enabledBtn(*op, "commandes.revoir"),
              "payee : Delivrer maintenant, Voir dans Stripe, Ouvrir le recu, Revoir...");
        check(click(*op, "commandes.revoir") && !rig.dialogs.empty(), "Revoir... : une question (la raison)");
        if (!rig.dialogs.empty()) {
            const std::string q = drawMenu(*rig.dialogs.back());
            check(has(q, "Refuser") && has(q, "ne sera pas livr"), "la question : refuser l'article (1.48.0 : une ligne, la raison)");
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
        check(has(t, "01234567") && !has(t, "Poste de Camille"), "sans demande scellee lisible : l'identifiant court du PC");
        check(has(t, "n'est pas celui de la commande") && enabledBtn(*op, "commandes.delivrer"), "a revoir : la raison ; Delivrer maintenant (apres revue)");
        check(pick(1), "choisir la n° 1 (a payer)");
        check(!enabledBtn(*op, "commandes.delivrer") && !enabledBtn(*op, "commandes.rembourser"), "a payer : ni livrer ni rembourser");
        // 1.47.0 : le filtre est une puce de la barre << Filtrer et chercher >> (Etat), plus une liste deroulante.
        auto* bar = find<ui::FilterBar>(*op, "commandes");
        if (bar) bar->setValues(5, {"pay\xC3\xA9" "e, \xC3\xA0 livrer"});
        (void)draw(*op);
        check(bar && table->model()->rowCount() == 1 && table->model()->cellText(0, 0) == "3", "la puce << Etat : payee, a livrer >> : la n° 3");
        if (bar) bar->clearAll();
        // un service 1.44.0 : la meme phrase sur la page des commandes
        license_admin::online::ServiceState st;
        st.reached = true;
        st.version = "1.44.0";
        ws.adoptServiceState(st);
        op->refresh();
        t = draw(*op);
        check(has(t, "d\xC3\xA9ploie la 1.46.0 pour la boutique") && visibleBtn(*op, "commandes.deployer"), "Commandes : un service 1.44.0, c'est dit");
        check(count(t, "1.46.0 pour la boutique") == 1 && !has(t, "Service : ") && !enabledBtn(*op, "commandes.delivrer"),
              "Commandes : un seul avertissement ; Delivrer attend le service 1.46.0");
        for (const auto& sz : kSizes) {
            (void)draw(*op, sz.w, sz.h);
            std::string why;
            check(columnsFit(*table, "\xC3\x89tat", why), std::string("les commandes (avec l'avertissement) a ") + sz.window + " : toutes les colonnes en vue (" + why + ")");
        }
        st.version = "1.46.0";
        ws.adoptServiceState(st);
    }

    // 1.48.1 : UN article rembourse - une commande payee de deux lignes (une licence, son option) ; Rembourser... nomme
    // l'article et son montant TTC ; Rembourser la commande... a part ; l'article rembourse : << remboursee >>, l'autre
    // reste << livree >>, et il ne se rembourse pas deux fois.
    std::printf("-- 1.48.1 : rembourser UN article (une commande de deux lignes)\n");
    {
        shop::Order o = order(7, "livree", "Atelier Exemple");
        o.linesKnown = true;
        o.lineCount = 2;
        o.amount = 49000 + 2900;
        o.licence = "XPG-2026-00011";
        shop::OrderLine a;
        a.line = 1;
        a.article = "ed:PROFESSIONAL";
        a.kind = "edition";
        a.edition = "PROFESSIONAL";
        a.amount = 49000;
        a.label = "Licence PROFESSIONAL - 1 an";
        a.state = "livree";
        a.licence = "XPG-2026-00011";
        shop::OrderLine b = a;
        b.line = 2;
        b.article = "opt:ia_avancee:30";
        b.kind = "option";
        b.edition.clear();
        b.option = "ia_avancee";
        b.days = 30;
        b.amount = 2900;
        b.label = "Option IA avanc\xC3\xA9" "e - 1 mois";
        b.withLine = 1;
        o.lines = {a, b};
        shop::OrdersView v;
        v.orders = {o};
        v.counter = 12;
        ws.adoptOrders(v);
        auto op = gui::makeOrdersPage(rig.host());
        op->refresh();
        (void)draw(*op);
        auto* table = find<ui::TableView>(*op, "commandes.table");
        const auto pickKey = [&](const std::string& key) {
            for (std::size_t i = 0; table && i < table->model()->rowCount(); ++i)
                if (table->model()->cellText(static_cast<ui::RowIndex>(i), 0) == key) {
                    table->selectModelRows({static_cast<ui::RowIndex>(i)});
                    return static_cast<int>(i);
                }
            return -1;
        };
        check(table && pickKey("7-2") >= 0, "la ligne 7-2 (l'option) choisie");
        std::string t = draw(*op);
        check(has(t, "article 2 sur 2") && enabledBtn(*op, "commandes.rembourser") && enabledBtn(*op, "commandes.rembourser-commande"),
              "une ligne livree : Rembourser... (cet article) et Rembourser la commande... (l'admin+)");
        check(click(*op, "commandes.rembourser") && !rig.dialogs.empty(), "Rembourser... : une question d'abord");
        if (!rig.dialogs.empty()) {
            const std::string q = drawMenu(*rig.dialogs.back());
            check(has(q, "Rembourser \xC2\xAB Option IA avanc\xC3\xA9" "e - 1 mois \xC2\xBB ?") && has(q, "29,00\xC2\xA0\xE2\x82\xAC") && has(q, "tels quels") &&
                      has(q, "XPG-2026-00011") && !has(q, "519,00"),
                  "la question nomme l'article et son montant TTC (29,00 EUR), les autres articles restent tels quels");
        }
        rig.dialogs.clear();
        check(click(*op, "commandes.rembourser-commande") && !rig.dialogs.empty(), "Rembourser la commande... : une question d'abord");
        if (!rig.dialogs.empty()) {
            const std::string q = drawMenu(*rig.dialogs.back());
            check(has(q, "Rembourser la commande n\xC2\xB0 7 ?") && has(q, "519,00\xC2\xA0\xE2\x82\xAC"), "toute la commande : 519,00 EUR");
        }
        rig.dialogs.clear();
        // l'article rembourse (le service l'a dit) : << remboursee >>, l'autre << livree >> ; pas deux fois
        v.orders[0].lines[1].state = "remboursee";
        ws.adoptOrders(v);
        op->refresh();
        (void)draw(*op);
        const int r1 = pickKey("7-1");
        check(r1 >= 0 && table->model()->cellText(static_cast<ui::RowIndex>(r1), 5) == "livr\xC3\xA9" "e", "la ligne 7-1 reste livree");
        const int r2 = pickKey("7-2");
        t = draw(*op);
        auto* refund = find<ui::Button>(*op, "commandes.rembourser");
        check(r2 >= 0 && table->model()->cellText(static_cast<ui::RowIndex>(r2), 5) == "rembours\xC3\xA9" "e" && refund && !refund->enabled() &&
                  has(refund->tooltip(), "d\xC3\xA9j\xC3\xA0 rembours\xC3\xA9"),
              "la ligne 7-2 : remboursee ; Rembourser... grise, la raison (" + (refund ? refund->tooltip() : std::string()) + ")");
        check(enabledBtn(*op, "commandes.rembourser-commande"), "toute la commande se rembourse encore (le reste)");
        // 1.48.1 : la puce << Type >> : achat, demande, demande de licence
        auto* bar = find<ui::FilterBar>(*op, "commandes");
        check(bar && table->model()->cellText(static_cast<ui::RowIndex>(r2), 8) == "Achat", "le type : Achat");
    }

    std::printf("-- le tableau de bord : a livrer\n");
    {
        ws.setOrdersToDeliver(2);
        auto dp = gui::makeDashboardPage(rig.host());
        TextRenderer r;
        (void)paint(*dp, r, 1141.f, 813.f);
        auto* tile = find<ui::Button>(*dp, "tableau.tuile.demandes");
        std::vector<std::string> in = tile ? drawnIn(r, *tile) : std::vector<std::string>{};
        const auto shows = [&](const std::string& s) { return std::find(in.begin(), in.end(), s) != in.end(); };
        check(tile && tile->text() == "Commandes \xC3\xA0 livrer" && shows("Commandes \xC3\xA0 livrer") && shows("2"), "la tuile : << Commandes a livrer >>, 2 en grand");
        check(!shows("0"), "jamais un 0 (ambre) dans la tuile des commandes a livrer");
        bool sub = false;
        for (const auto& s : in)
            if (has(s, "pay\xC3\xA9" "es dans la boutique")) sub = true;
        check(sub, "dessous : << payees dans la boutique >>");
        check(click(*dp, "tableau.tuile.demandes") && !rig.navigations.empty() && rig.navigations.back() == "commandes", "son clic : Commandes");
        ws.setOrdersToDeliver(0);
        dp->refresh();
        (void)paint(*dp, r, 1141.f, 813.f);
        in = drawnIn(r, *tile);
        check(tile->text() == "Demandes en attente" && !has(r.all(), "\xC3\xA0 livrer"), "rien a livrer : la tuile des demandes, rien n'est dit");
    }

    std::printf("-- Brancher Stripe... : la cle masquee, jamais gardee\n");
    {   // pas encore branche : << Brancher Stripe >>
        shop::Settings s = ws.shop();
        s.stripe = shop::StripeState{};
        ws.adoptShop(s);
        auto d = gui::makeStripeDialog(rig.host());
        (void)d->Initialize();
        const std::string t = drawMenu(*d);
        auto* wm = dynamic_cast<menu::WidgetMenu*>(d.get());
        ui::Widget& root = *wm->widgetRoot();
        auto* go = find<ui::Button>(root, "stripe.brancher");
        check(has(t, "Brancher Stripe") && !has(t, "Rebrancher") && go && go->text() == "Brancher" && !has(t, "remplace l'ancienne"), "pas encore branche : Brancher Stripe, Brancher");
        auto* key = find<ui::InputText>(root, "stripe.cle");
        check(key && go && go->bounds().y - key->bounds().bottom() <= 90.f, "le cadre suit son contenu (pas de vide sous le champ)");
    }
    {   // deja branche (mode test) : << Rebrancher Stripe >>, la nouvelle cle remplace l'ancienne
        shop::Settings s = ws.shop();
        s.stripe.connected = true;
        s.stripe.webhook = true;
        s.stripe.mode = "test";
        ws.adoptShop(s);
        auto d = gui::makeStripeDialog(rig.host());
        (void)d->Initialize();
        std::string t = drawMenu(*d);
        auto* wm = dynamic_cast<menu::WidgetMenu*>(d.get());
        ui::Widget& root = *wm->widgetRoot();
        auto* key = find<ui::InputText>(root, "stripe.cle");
        auto* go = find<ui::Button>(root, "stripe.brancher");
        check(has(t, "Rebrancher Stripe") && has(t, "D\xC3\xA9j\xC3\xA0 branch\xC3\xA9 (mode test) : la nouvelle cl\xC3\xA9 remplace l'ancienne") && go && go->text() == "Rebrancher",
              "deja branche : Rebrancher Stripe, le mode, la nouvelle cle remplace l'ancienne ; le bouton Rebrancher");
        check(has(t, "Colle ta cl\xC3\xA9 secr\xC3\xA8te Stripe") && key && key->masked(), "le dialogue : la cle dans un champ masque");
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
