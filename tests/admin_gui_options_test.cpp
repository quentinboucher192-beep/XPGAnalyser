// =============================================================================
//  tests/admin_gui_options_test.cpp - 1.42.0 : l'application de l'editeur, les
//  OFFRES TEMPORAIRES et les OPTIONS a l'ecran
// -----------------------------------------------------------------------------
//  A blanc (TextRenderer, HostRig) sur un poste Admin d'essai (cle d'ESSAI,
//  registre d'exemple, PC fictifs) :
//   - Offres : << En vente : pendant une periode >> (le mini calendrier, du ... au
//     ... compris), signer : la periode dans le registre ; supprimer une offre
//     qu'aucune licence n'utilise (la confirmation) ; une offre utilisee : << Retirer
//     de la vente >> a la place ;
//   - Options : une option creee (son nom, ce qu'elle ouvre, ses durees et prix),
//     signee avec le registre ;
//   - Licences : << Ajouter une option... >> (la fenetre : l'option, la duree, ce
//     que le client aura), la nouvelle emission ecrite ou on l'a choisie, la fiche :
//     << Options (1 en cours) >> et << Achats du client >>.
//  argv[1] : la racine du depot. Aucune donnee reelle.
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/ui/widgets/Controls.hpp"
#include "../src/ui/widgets/DataViews.hpp"
#include "../src/ui/widgets/DatePicker.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace admintest;

namespace {

constexpr lic::Seconds kDay = 86400;

std::string paintDialog(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1280.f, 800.f);
}

std::string paintPage(ui::Widget& w, float width = 1280.f, float height = 800.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}

ui::TableView* table(ui::Widget& root, std::string_view id) { return find<ui::TableView>(root, id); }

std::size_t rows(ui::Widget& root, std::string_view id) {
    auto* t = table(root, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}

int rowOf(ui::Widget& root, std::string_view id, std::size_t col, const std::string& text) {
    auto* t = table(root, id);
    if (!t || !t->model()) return -1;
    for (std::size_t r = 0; r < t->model()->rowCount(); ++r)
        if (t->model()->cellText(static_cast<ui::RowIndex>(r), col) == text) return static_cast<int>(r);
    return -1;
}

bool selectRow(ui::Widget& root, std::string_view id, std::size_t col, const std::string& text) {
    const int r = rowOf(root, id, col, text);
    if (r < 0) return false;
    table(root, id)->selectModelRows({static_cast<ui::RowIndex>(r)}, true);
    return true;
}

bool closed(menu::IMenu& m) { return m.widgetRoot() && !m.widgetRoot()->visible(); }

bool shown(ui::Widget& root, std::string_view id) {
    auto* w = root.findById(id);
    return w && w->visible();
}

bool enabledBtn(ui::Widget& root, std::string_view id) {
    auto* b = find<ui::Button>(root, id);
    return b && b->enabled() && b->visible();
}

bool pickItem(ui::Widget& root, std::string_view id, const std::string& value) {
    auto* d = find<ui::DropDown>(root, id);
    if (!d) return false;
    for (std::size_t i = 0; i < d->items().size(); ++i)
        if (d->items()[i].value == value) {
            d->setSelectedIndex(static_cast<int>(i));
            return true;
        }
    return false;
}

bool setCheck(ui::Widget& root, std::string_view id, bool on) {
    auto* c = find<ui::Checkbox>(root, id);
    if (!c) return false;
    c->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    return true;
}

// Les jours (00:00 UTC) d'aujourd'hui + n.
lic::Seconds dayPlus(int n) { return ui::DatePicker::dayOf(g_now) + static_cast<lic::Seconds>(n) * kDay; }

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_options_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    Fixture fx(source, "options-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    const std::string reqA = fx.request("pcA", "Poste essai A");
    check(fx.licence("XPG-2026-00301", "Client d'essai Omega", "PROFESSIONAL", {reqA}, 365).ok, "une licence PROFESSIONAL d'un an (par fichier)");
    HostRig rig(fx);
    auto h = rig.host();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Offres : une offre temporaire (en vente du ... au ..., le mini calendrier)\n");
    {
        auto page = gui::makeEditionsPage(h);
        auto& p = *page;
        (void)paintPage(p);
        check(selectRow(p, "editions.table", 0, "ENTERPRISE"), "ENTERPRISE choisie");
        check(!shown(p, "editions.vente-debut") || find<ui::DatePicker>(p, "editions.vente-debut")->date() == 0, "en vente toujours : pas de periode");
        check(pickItem(p, "editions.vente", "periode"), "En vente : pendant une periode");
        (void)paintPage(p);
        auto* from = find<ui::DatePicker>(p, "editions.vente-debut");
        auto* to = find<ui::DatePicker>(p, "editions.vente-fin");
        check(from && to && shown(p, "editions.vente-debut") && shown(p, "editions.vente-fin"), "les deux calendriers (Du, Au)");
        check(from && from->date() == dayPlus(0) && to && to->date() == dayPlus(6), "par defaut : une semaine, d'aujourd'hui au jour + 6 (compris)");
        if (to) {
            to->open();
            check(to->isOpen() && to->dayRect(dayPlus(13)).w > 0, "Au : le mois s'ouvre, les jours se cliquent");
            to->pick(dayPlus(13));
            check(!to->isOpen() && to->date() == dayPlus(13), "un jour choisi : le mois se ferme");
        }
        std::string t = paintPage(p);
        check(has(t, ui::DatePicker::frenchDay(dayPlus(13)).substr(5)), "le champ montre le jour choisi");
        check(enabledBtn(p, "editions.signer") && click(p, "editions.signer"), "Signer le registre");
        const auto* e = fx.ws->registry().edition("ENTERPRISE");
        check(e && e->saleFrom == dayPlus(0) && e->saleUntil == dayPlus(14) && e->temporary() && e->onSale(g_now), "le registre signe : en vente du jour au jour + 13 compris");
        check(e && !e->onSale(dayPlus(14)), "le lendemain du dernier jour : plus en vente");
        t = paintPage(p);
        check(has(t, "temporaire"), "la carte de l'offre : temporaire");
        // une offre utilisee par une licence en cours ne se supprime pas
        check(selectRow(p, "editions.table", 0, "PROFESSIONAL"), "PROFESSIONAL choisie (une licence en cours)");
        const std::size_t before = rig.dialogs.size();
        check(click(p, "editions.supprimer") && rig.dialogs.size() == before + 1, "Supprimer l'offre... : une fenetre");
        if (rig.dialogs.size() == before + 1) {
            auto& d = *rig.dialogs.back();
            t = paintDialog(d);
            check(has(t, "ne peut pas \xC3\xAAtre supprim\xC3\xA9" "e") && has(t, "XPG-2026-00301") && has(t, "Retirer de la vente"),
                  "utilisee : elle le dit (la licence), propose de la retirer de la vente");
            check(click(*d.widgetRoot(), "confirmer.ok"), "Retirer de la vente");
            check(rowOf(p, "editions.table", 0, "PROFESSIONAL") >= 0 && enabledBtn(p, "editions.signer"), "retiree dans le brouillon (rien n'est supprime)");
            const auto pending = fx.ws->offersToDelete();
            check(std::find(pending.begin(), pending.end(), "PROFESSIONAL") != pending.end(), "notee : supprimee d'elle-meme apres sa derniere licence");
        }
        check(click(p, "editions.annuler"), "Annuler les modifications");
        // une offre sans licence se supprime (confirmation)
        const std::size_t offers = rows(p, "editions.table");
        check(selectRow(p, "editions.table", 0, "PERSONAL"), "PERSONAL choisie (aucune licence)");
        check(click(p, "editions.supprimer") && !rig.dialogs.empty(), "Supprimer l'offre... : la confirmation");
        if (!rig.dialogs.empty()) {
            auto& d = *rig.dialogs.back();
            t = paintDialog(d);
            check(has(t, "Supprimer l'offre PERSONAL"), "la confirmation la nomme");
            check(click(*d.widgetRoot(), "confirmer.ok") && rows(p, "editions.table") == offers - 1 && rowOf(p, "editions.table", 0, "PERSONAL") < 0,
                  "supprimee du brouillon");
        }
        check(click(p, "editions.annuler") && rowOf(p, "editions.table", 0, "PERSONAL") >= 0, "Annuler les modifications : elle revient");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Options : creer une option, ce qu'elle ouvre, ses durees et prix, signer\n");
    {
        auto page = gui::makeOptionsPage(h);
        auto& p = *page;
        std::string t = paintPage(p);
        check(p.title() == "Options" && rows(p, "options.table") == 3 && rowOf(p, "options.table", 0, "exports_avances") >= 0 &&
                  rowOf(p, "options.table", 0, "ia_avancee") >= 0 && rowOf(p, "options.table", 0, "travail_a_plusieurs") >= 0,
              "la page Options : les 3 options des offres par defaut");
        check(click(p, "options.nouvelle") && shown(p, "options.nouvelle.id"), "Nouvelle option... : le formulaire");
        check(type(p, "options.nouvelle.id", "Rapports Avances") && click(p, "options.nouvelle.creer") && rows(p, "options.table") == 3, "un identifiant mal ecrit : refuse");
        check(type(p, "options.nouvelle.id", "exports_avances") && click(p, "options.nouvelle.creer") && rows(p, "options.table") == 3, "un identifiant deja pris : refuse");
        check(type(p, "options.nouvelle.id", "rapports_avances") && type(p, "options.nouvelle.nom", "Rapports avanc\xC3\xA9s") && click(p, "options.nouvelle.creer") &&
                  rows(p, "options.table") == 4,
              "rapports_avances creee");
        t = paintPage(p);
        auto* feats = table(p, "options.fonctionnalites");
        const int adv = rowOf(p, "options.fonctionnalites", 1, "ai_advanced_models");
        check(feats && adv >= 0, "la liste de ce qu'elle peut ouvrir");
        if (feats && adv >= 0) feats->checkClicked->emit(static_cast<ui::RowIndex>(adv));
        check(setCheck(p, "options.duree-30", true) && type(p, "options.prix-30", "49 \xE2\x82\xAC HT") && type(p, "options.prix-7", "19 \xE2\x82\xAC HT"),
              "1 semaine (19) et 1 mois (49)");
        check(setCheck(p, "options.boutique", true), "dans la boutique des Studios");
        check(enabledBtn(p, "options.signer") && click(p, "options.signer"), "Signer le registre");
        const auto* o = fx.ws->registry().option("rapports_avances");
        check(o && o->label == "Rapports avanc\xC3\xA9s" && o->features.size() == 1 && o->features.front().id == "ai_advanced_models" && o->durations.size() == 2 &&
                  o->shop,
              "le registre signe : l'option, ce qu'elle ouvre, ses deux durees");
        check(o && o->durations.front().days == 7 && o->durations.front().price == "19 \xE2\x82\xAC HT" && o->durations.back().days == 30 &&
                  o->durations.back().price == "49 \xE2\x82\xAC HT",
              "les prix de chaque duree");
        t = paintPage(p);
        check(has(t, "Rapports avanc\xC3\xA9s") && has(t, "toutes"), "la table : proposee a toutes les offres");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Licences : Ajouter une option... (la fenetre, la nouvelle emission, la fiche)\n");
    {
        const std::string id = "XPG-2026-00301";
        check(!fx.ws->licence(id)->licence.grant("ai_advanced_models"), "(PROFESSIONAL n'ouvre pas ai_advanced_models)");
        auto page = gui::makeLicencesPage(h);
        rig.refreshers.push_back([&] { page->refresh(); });
        auto& p = *page;
        (void)paintPage(p);
        check(selectRow(p, "licences.table", 0, id), "la licence choisie");
        (void)paintPage(p);
        check(shown(p, "licences.ajouter-option") && enabledBtn(p, "licences.ajouter-option"), "le bouton Ajouter une option... (le catalogue en a une)");
        const std::size_t before = rig.dialogs.size();
        check(click(p, "licences.ajouter-option") && rig.dialogs.size() == before + 1, "la fenetre s'ouvre");
        if (rig.dialogs.size() == before + 1) {
            auto& d = *rig.dialogs.back();
            ui::Widget& w = *d.widgetRoot();
            std::string t = paintDialog(d);
            check(has(t, "Ajouter une option") && has(t, id) && has(t, "Client d'essai Omega"), "le titre : la licence, son client");
            check(pickItem(w, "option.choix", "rapports_avances"), "l'option : Rapports avances");
            check(pickItem(w, "option.duree", "30"), "1 mois");
            t = paintDialog(d);
            check(find<ui::InputText>(w, "option.prix") && find<ui::InputText>(w, "option.prix")->text() == "49 \xE2\x82\xAC HT", "le prix du catalogue repris");
            check(has(t, "Elle ouvre") && has(t, "elle s'arr\xC3\xAA" "te seule") && has(t, "1.42"), "ce que le client aura (et la 1.42 qu'il lui faut)");
            const std::string out = U(fx.root / "sortie" / (id + "-option.xpglic"));
            rig.picks.push_back(out);
            g_now += 60;   // une nouvelle emission : pas dans la seconde de la precedente
            check(click(w, "option.ok"), "Ajouter l'option");
            check(closed(d), "la fenetre se ferme");
            check(!rig.asked.empty() && rig.asked.back().save, "on choisit ou ecrire la nouvelle emission");
            check(rig.last() && rig.last()->ok, "signee : " + (rig.last() ? rig.last()->text() : std::string()));
            const auto* e = fx.ws->licence(id);
            check(e && e->licence.options.size() == 1 && e->licence.options.front().id == "rapports_avances" && e->licence.options.front().price == "49 \xE2\x82\xAC HT",
                  "la licence porte l'option (et son prix)");
            check(e && !e->licence.options.empty() && e->licence.options.front().until == e->licence.options.front().from + 30 * kDay, "pour 30 jours");
            check(e && lic::effectiveGrants(e->licence, g_now + kDay).size() > e->licence.features.size(), "ses droits : ai_advanced_models en plus pendant l'option");
            check(e && lic::effectiveGrants(e->licence, g_now + 40 * kDay).size() == e->licence.features.size(), "apres : comme avant (elle s'arrete seule)");
            check(fs::exists(fs::path(out)), "le fichier ecrit la ou on l'a choisi");
        }
        (void)selectRow(p, "licences.table", 0, id);
        const std::string t = paintPage(p, 1400.f, 2600.f);   // haute : toute la fiche (ses categories du bas)
        check(has(t, "Options (1 en cours)"), "la fiche : Options (1 en cours)");
        check(has(t, "Achats du client ("), "la fiche : Achats du client");
        check(has(t, "Rapports avanc\xC3\xA9s"), "l'option nommee dans la fiche");
        const auto hist = fx.ws->purchaseHistory("Client d'essai Omega");
        check(std::any_of(hist.begin(), hist.end(), [](const lic::Purchase& x) { return x.kind == "option" && x.label.rfind("Rapports avanc\xC3\xA9s", 0) == 0; }),
              "l'historique du client : l'option achetee");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Licences : sans option au catalogue, pas de bouton (un registre sans options)\n");
    {
        Fixture f2(source, "options-gui-sans");
        {   // le registre d'exemple sans son catalogue d'options (un registre d'avant la 1.42)
            std::ifstream in(fs::path(source) / "tests" / "fixtures" / "licences" / "registre-exemple.json", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const auto at = text.find("\"options\"");
            const auto comma = at == std::string::npos ? std::string::npos : text.rfind(',', at);
            if (comma != std::string::npos) text = text.substr(0, comma) + "\n}\n";
            std::ofstream(f2.root / "registre-sans-options.json", std::ios::binary) << text;
        }
        check(f2.ws->createKey("essai application", true).ok && f2.ws->signRegistryJson(U(f2.root / "registre-sans-options.json")).ok && f2.ready() &&
                  f2.ws->registry().options.empty(),
              "un autre poste d'essai, son registre sans option");
        check(f2.licence("XPG-2026-00302", "Client d'essai Sigma", "PROFESSIONAL", {f2.request("pcB", "Poste essai B")}, 365).ok, "une licence");
        HostRig r2(f2);
        auto p2 = gui::makeLicencesPage(r2.host());
        (void)paintPage(*p2);
        (void)selectRow(*p2, "licences.table", 0, "XPG-2026-00302");
        (void)paintPage(*p2);
        check(!shown(*p2, "licences.ajouter-option"), "le registre n'a pas d'option : le bouton n'est pas montre");
    }

    return finish();
}
