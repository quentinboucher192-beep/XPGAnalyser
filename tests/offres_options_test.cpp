// =============================================================================
//  tests/offres_options_test.cpp - 1.42.0 : les offres temporaires, les options,
//  les achats, les clients supprimes
// -----------------------------------------------------------------------------
//  Le format (licensing/Documents) : les champs de la 1.42 ne s'ecrivent que
//  s'ils servent (un registre ou une licence sans eux reste celui de la 1.41),
//  ils se relisent, ils sont controles. Le poste Admin d'essai
//  (admin_test_fixture.hpp, une cle d'ESSAI, des noms d'essai) : une option au
//  catalogue, une licence PERSONAL, l'option ajoutee pour une semaine ; le PC
//  du client l'importe : la fonctionnalite s'ouvre, puis se referme seule a la
//  fin de la semaine. L'historique des achats, la suppression d'un client (refusee
//  tant qu'une licence court), sa restauration, les offres a supprimer.
// =============================================================================
#include "admin_test_fixture.hpp"

using namespace admintest;

namespace {

bool hasFeature(const std::vector<lic::FeatureGrant>& g, const std::string& id) {
    return std::any_of(g.begin(), g.end(), [&](const lic::FeatureGrant& x) { return x.id == id; });
}

lic::OptionDef exportsOption() {
    lic::OptionDef o;
    o.id = "exports_avances";
    o.label = "Exports avanc\xC3\xA9s";
    o.description = "PDF et Excel multi-pages";
    o.features = {{"advanced_export", {}}};
    o.editions = {"PERSONAL"};
    o.durations = {{7, "19 \xE2\x82\xAC HT"}, {30, "49 \xE2\x82\xAC HT"}, {0, "290 \xE2\x82\xAC HT / an"}};
    return o;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    if (!deploy::crypto::init()) return 1;
    Fixture f(source, "offres");
    auto& ws = *f.ws;
    check(f.ready(), "le poste d'essai : la cle d'essai, le registre d'exemple");
    const lic::Seconds kDay = lic::kDay;

    std::printf("-- le format : rien de nouveau tant que rien ne sert\n");
    {
        const lic::Registry& r = ws.registry();
        const std::string text = lic::writeRegistry(r);
        check(!r.usesFormat142() && !has(text, "sale_") && !has(text, "\"options\""), "le registre d'exemple : aucun champ de la 1.42 ecrit");
        lic::Registry x = r;
        lic::EditionDef* pro = nullptr;
        for (auto& e : x.editions)
            if (e.id == "PROFESSIONAL") pro = &e;
        check(pro != nullptr, "l'offre PROFESSIONAL est dans le registre d'exemple");
        if (pro) {
            pro->saleFrom = g_now + kDay;
            pro->saleUntil = g_now + 8 * kDay;
        }
        x.options = {exportsOption()};
        const std::string t2 = lic::writeRegistry(x);
        lic::Registry back;
        std::string why;
        check(lic::parseRegistry(t2, back, &why) && back.options.size() == 1 && back.options[0] == x.options[0] && back.usesFormat142(),
              "une offre temporaire et une option : ecrites, relues a l'identique (" + why + ")");
        const lic::EditionDef* bp = back.edition("PROFESSIONAL");
        check(bp && bp->saleFrom == g_now + kDay && bp->saleUntil == g_now + 8 * kDay && bp->temporary(), "la periode de vente relue");
        check(bp && !bp->onSale(g_now) && bp->onSale(g_now + 2 * kDay) && !bp->onSale(g_now + 8 * kDay), "en vente seulement pendant sa periode (fin exclue)");
        // controles
        lic::Registry bad = x;
        bad.options[0].features = {{"inconnue_xyz", {}}};
        check(!lic::parseRegistry(lic::writeRegistry(bad), back, &why) && has(why, "inconnue_xyz"), "une option qui ouvre une fonctionnalite non declaree : refusee (" + why + ")");
        bad = x;
        bad.options[0].editions = {"NULLE"};
        check(!lic::parseRegistry(lic::writeRegistry(bad), back, &why) && has(why, "NULLE"), "une option proposee a une offre absente : refusee");
        bad = x;
        bad.options[0].durations.clear();
        check(!lic::parseRegistry(lic::writeRegistry(bad), back, &why), "une option sans duree : refusee");
        bad = x;
        for (auto& e : bad.editions)
            if (e.id == "PROFESSIONAL") e.saleUntil = e.saleFrom;
        check(!lic::parseRegistry(lic::writeRegistry(bad), back, &why) && has(why, "finit avant"), "une vente qui finit avant de commencer : refusee");
        // une licence
        lic::License l;
        l.licenseId = "XPG-ESSAI-1";
        l.customer = "Client d'essai";
        l.edition = "PERSONAL";
        l.type = lic::LicenseType::Temporary;
        l.service = "https://xpg-licences.essai.workers.dev";
        l.maxDevices = 2;
        l.features = {{"api_editor", {}}};
        l.issuedAt = g_now;
        l.expiresAt = g_now + 365 * kDay;
        l.manifest = 3;
        l.nonce = std::string(32, 'a');
        const std::string plain = lic::writeLicense(l);
        check(!has(plain, "\"options\"") && !has(plain, "\"purchases\""), "une licence sans option : aucun champ de la 1.42 ecrit");
        lic::LicenseOption o;
        o.id = "exports_avances";
        o.label = "Exports avanc\xC3\xA9s";
        o.features = {{"advanced_export", {}}};
        o.from = g_now;
        o.until = g_now + 7 * kDay;
        o.price = "19 \xE2\x82\xAC HT";
        l.options = {o};
        l.purchases = {{g_now, "option", "Exports avanc\xC3\xA9s \xC2\xB7 1 semaine", "XPG-ESSAI-1", g_now, g_now + 7 * kDay, "19 \xE2\x82\xAC HT"}};
        lic::License lb;
        check(lic::parseLicense(lic::writeLicense(l), lb, &why) && lb.options == l.options && lb.purchases == l.purchases, "options et achats : ecrits, relus (" + why + ")");
        check(!hasFeature(lic::effectiveGrants(l, g_now - 1), "advanced_export") && hasFeature(lic::effectiveGrants(l, g_now), "advanced_export") &&
                  hasFeature(lic::effectiveGrants(l, g_now + 7 * kDay - 1), "advanced_export") && !hasFeature(lic::effectiveGrants(l, g_now + 7 * kDay), "advanced_export"),
              "une option ouvre sa fonctionnalite de son debut a sa fin (exclue), seulement");
        l.options[0].until = 0;
        check(hasFeature(lic::effectiveGrants(l, g_now + 300 * kDay), "advanced_export") && !hasFeature(lic::effectiveGrants(l, g_now + 365 * kDay), "advanced_export"),
              "une option << jusqu'a la fin de la licence >> s'arrete avec elle");
        check(lic::checkAgainstRegistry(l, r, &why), "la licence tient avec le registre (" + why + ")");
        l.options[0].features = {{"inconnue_xyz", {}}};
        check(!lic::checkAgainstRegistry(l, r, &why) && has(why, "inconnue_xyz"), "une option qui ouvre une fonctionnalite inconnue du registre : refusee");
        l.purchases[0].kind = "cadeau";
        check(!lic::parseLicense(lic::writeLicense(l), lb, &why) && has(why, "cadeau"), "un genre d'achat inconnu : refuse");
    }

    std::printf("-- l'Admin : une option au catalogue, une licence, l'option ajoutee\n");
    {
        lic::Registry edited = ws.registry();
        edited.options = {exportsOption()};
        const auto sr = ws.signRegistry(edited);
        check(sr.ok && ws.registry().option("exports_avances") && ws.registry().usesFormat142(), "le registre avec l'option : signe (" + sr.text() + ")");
    }
    const std::string req = f.request("pc-meyer", "Poste essai Meyer");
    const auto signedR = f.licence("XPG-2026-00101", "Atelier d'essai", "PERSONAL", {req});
    check(signedR.ok, "une licence PERSONAL signee (" + signedR.text() + ")");
    const auto* entry = ws.licence("XPG-2026-00101");
    check(entry && entry->licence.purchases.size() == 1 && entry->licence.purchases[0].kind == "offer" && has(entry->licence.purchases[0].label, "1 an"),
          "le registre en 1.42 : la licence porte son achat (l'offre, 1 an)");
    check(ws.licencesInForce("PERSONAL").size() == 1 && ws.lastLicenceEnd("PERSONAL") == g_now + 365 * kDay, "l'offre PERSONAL : 1 licence en vigueur, sa fin");

    // le PC du client : sans l'option, la fonctionnalite est fermee
    lic::LicenseStore pc(f.pcOptions(f.root / "pc-meyer"));
    {
        const auto rep = pc.importFile(U(f.root / "sortie" / "XPG-2026-00101.xpglic"));
        check(rep.ok && rep.state.status == lic::LicenseStatus::Valid && !rep.state.hasFeature("advanced_export"), "le PC : la licence PERSONAL, sans les exports avances");
        check(rep.state.purchases.size() == 1, "le PC : son historique des achats (1)");
    }
    gui::OptionDraft od;
    od.optionId = "exports_avances";
    od.days = 7;
    check(!ws.addOption("XPG-2026-00999", od, U(f.root / "sortie" / "x.xpglic")).ok, "une licence absente : refusee");
    gui::OptionDraft bogus = od;
    bogus.optionId = "absente";
    check(!ws.addOption("XPG-2026-00101", bogus, U(f.root / "sortie" / "x.xpglic")).ok, "une option absente du catalogue : refusee");
    g_now += 60;   // une minute apres la licence (l'archive garde l'emission la plus recente)
    const auto added = ws.addOption("XPG-2026-00101", od, U(f.root / "sortie" / "XPG-2026-00101-option.xpglic"));
    check(added.ok, "l'option ajoutee (" + added.text() + ")");
    entry = ws.licence("XPG-2026-00101");
    check(entry && entry->licence.options.size() == 1 && entry->licence.options[0].from == g_now && entry->licence.options[0].until == g_now + 7 * kDay &&
              entry->licence.options[0].price == "19 \xE2\x82\xAC HT" && entry->licence.devices.size() == 1,
          "la nouvelle emission : l'option (du jour, 7 jours, 19 EUR), le meme PC");
    check(entry && entry->licence.purchases.size() == 2 && entry->licence.purchases[1].kind == "option", "l'historique : l'offre, puis l'option");
    {
        const auto rep = pc.importFile(U(f.root / "sortie" / "XPG-2026-00101-option.xpglic"));
        check(rep.ok && rep.state.hasFeature("advanced_export") && rep.state.options.size() == 1 && rep.state.purchases.size() == 2,
              "le PC importe l'emission : les exports avances s'ouvrent, l'option et 2 achats sont la (" + rep.why + ")");
    }
    const lic::Seconds before = g_now;
    g_now += 8 * kDay;
    check(!pc.evaluate().hasFeature("advanced_export") && pc.evaluate().status == lic::LicenseStatus::Valid,
          "8 jours plus tard : les exports avances se referment seuls, la licence reste valide");
    g_now = before;
    const auto hist = ws.purchaseHistory("ATELIER D'ESSAI");
    check(hist.size() == 2 && hist[0].kind == "offer" && hist[1].kind == "option" && has(hist[1].label, "1 semaine"), "l'historique du client (sans casse) : 2 achats");
    gui::OptionDraft late = od;
    late.from = g_now + 400 * kDay;
    check(!ws.addOption("XPG-2026-00101", late, U(f.root / "sortie" / "y.xpglic")).ok, "une option qui commencerait apres la licence : refusee");

    std::printf("-- supprimer un client\n");
    {
        const auto refused = ws.deleteClient("Atelier d'essai");
        check(!refused.ok && has(refused.message, "en cours"), "une licence en cours : la suppression est refusee (" + refused.message + ")");
        const auto rv = ws.revoke("XPG-2026-00101", "", "Essai de suppression", false);
        check(rv.ok && ws.licencesInForce("PERSONAL").empty(), "la licence revoquee : plus en vigueur");
        const auto del = ws.deleteClient("Atelier d'essai");
        check(del.ok && !ws.licence("XPG-2026-00101"), "supprime : sa licence quitte l'archive (" + del.text() + ")");
        const auto cl = ws.clients();
        check(std::none_of(cl.begin(), cl.end(), [](const gui::ClientEntry& c) { return c.name == "Atelier d'essai"; }), "il n'est plus dans les clients (le registre des activations ne le ramene pas)");
        const auto dc = ws.deletedClients();
        check(dc.size() == 1 && dc[0].licenceIds.size() == 1 && fs::exists(dc[0].trashFiles[0]), "la corbeille des clients : lui, sa licence, son fichier");
        const auto res = ws.restoreClient("Atelier d'essai");
        check(res.ok && ws.licence("XPG-2026-00101") && ws.deletedClients().empty(), "restaure : sa licence revient (" + res.text() + ")");
        const auto cl2 = ws.clients();
        check(std::any_of(cl2.begin(), cl2.end(), [](const gui::ClientEntry& c) { return c.name == "Atelier d'essai"; }), "il revient dans les clients");
        check(!ws.restoreClient("Personne").ok, "restaurer un client absent : refuse");
    }

    std::printf("-- les offres a supprimer d'elles-memes\n");
    {
        check(ws.offersToDelete().empty(), "aucune au depart");
        check(ws.setDeleteWhenUnused("PERSONAL", true).ok && ws.offersToDelete() == std::vector<std::string>{"PERSONAL"}, "PERSONAL notee");
        check(ws.setDeleteWhenUnused("PERSONAL", false).ok && ws.offersToDelete().empty(), "puis retiree de la liste");
    }
    return finish();
}
