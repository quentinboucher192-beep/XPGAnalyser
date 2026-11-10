// =============================================================================
//  tests/admin_gui_donnees_test.cpp - 1.33.0 : les pages de DONNEES de
//  l'application de l'editeur (Editions, Fonctionnalites, Activations, Clients,
//  Historique), dessinees a blanc et manipulees
// -----------------------------------------------------------------------------
//  Le poste Admin d'essai (admin_test_fixture.hpp : une cle d'ESSAI, le registre
//  d'exemple n. 318, des PC fictifs "Poste essai A"...) ; aucune donnee reelle.
//  argv[1] : la racine du depot.
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"   // 1.47.0 : la barre de filtres des listes
#include "admin_gui_harness.hpp"

#include "../src/license_admin/Admin.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace license_admin::gui {
// PageHistory.cpp : le CSV de l'historique (la regle des formules de devicesCsv).
std::string historyCsv(const std::vector<ActivationRecord>& records);
} // namespace license_admin::gui

using namespace admintest;

namespace {

ui::TableView* table(ui::Widget& page, std::string_view id) { return find<ui::TableView>(page, id); }

std::size_t rows(ui::Widget& page, std::string_view id) {
    auto* t = table(page, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}

std::string cell(ui::Widget& page, std::string_view id, std::size_t r, std::size_t c) {
    auto* t = table(page, id);
    return t && t->model() && r < t->model()->rowCount() ? t->model()->cellText(static_cast<ui::RowIndex>(r), c) : std::string();
}

int rowOf(ui::Widget& page, std::string_view id, std::size_t col, const std::string& text) {
    auto* t = table(page, id);
    if (!t || !t->model()) return -1;
    for (std::size_t r = 0; r < t->model()->rowCount(); ++r)
        if (t->model()->cellText(static_cast<ui::RowIndex>(r), col) == text) return static_cast<int>(r);
    return -1;
}

bool selectRow(ui::Widget& page, std::string_view id, std::size_t col, const std::string& text) {
    const int r = rowOf(page, id, col, text);
    if (r < 0) return false;
    table(page, id)->selectModelRows({static_cast<ui::RowIndex>(r)}, true);
    return true;
}

std::string fieldText(ui::Widget& page, std::string_view id) {
    auto* f = find<ui::InputText>(page, id);
    return f ? f->text() : std::string("<absent>");
}

bool shown(ui::Widget& page, std::string_view id) {
    auto* w = page.findById(id);
    return w && w->visible();
}

bool enabledBtn(ui::Widget& page, std::string_view id) {
    auto* b = find<ui::Button>(page, id);
    return b && b->enabled() && b->visible();
}

std::string btnText(ui::Widget& page, std::string_view id) {
    auto* b = find<ui::Button>(page, id);
    return b ? b->text() : std::string();
}

bool setCheck(ui::Widget& page, std::string_view id, bool on) {
    auto* c = find<ui::Checkbox>(page, id);
    if (!c) return false;
    c->setState(on ? ui::Checkbox::State::Checked : ui::Checkbox::State::Unchecked);
    return true;
}

bool pickDrop(ui::Widget& page, std::string_view id, const std::string& value) {
    auto* d = find<ui::DropDown>(page, id);
    if (!d) return false;
    for (std::size_t i = 0; i < d->items().size(); ++i)
        if (d->items()[i].value == value) {
            d->setSelectedIndex(static_cast<int>(i));
            return true;
        }
    return false;
}

// 1.47.0 : la puce Type de la barre de l'historique (avant : la liste deroulante historique.type) ; "" : la puce effacee.
bool pickType(ui::Widget& page, const std::string& kind) {
    auto* bar = find<ui::FilterBar>(page, "historique");
    if (!bar) return false;
    const int f = bar->fieldByTitle("Type");
    if (f < 0) return false;
    if (kind.empty()) bar->clearField(static_cast<std::size_t>(f));
    else if (kind == "demande") bar->setValues(static_cast<std::size_t>(f), {"Demande"});
    else if (kind == "licence") bar->setValues(static_cast<std::size_t>(f), {"Licence"});
    else if (kind == "extension") bar->setValues(static_cast<std::size_t>(f), {"Extension"});
    else if (kind == "revocation") bar->setValues(static_cast<std::size_t>(f), {"R\xC3\xA9vocation"});
    else if (kind == "client") bar->setValues(static_cast<std::size_t>(f), {"Client supprim\xC3\xA9", "Client restaur\xC3\xA9"});
    else return false;
    return true;
}

// Un travail lent qui dure (pour essayer les refus pendant qu'il tourne).
bool startSlow(HostRig& rig, gui::ShellHost& h) {
    rig.slowOnThread = true;
    return h.runSlow("essai lent", [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        return gui::OpResult::done("Travail d'essai fini.");
    }, {});
}

// Chaque bouton visible tient dans la page.
bool buttonsInside(ui::Widget& w, const gfx::Rect& page) {
    for (const auto& c : w.children()) {
        if (!c->visible()) continue;
        if (auto* b = dynamic_cast<ui::Button*>(c.get())) {
            const auto r = b->bounds();
            if (r.x < page.x - 0.5f || r.y < page.y - 0.5f || r.right() > page.right() + 0.5f || r.bottom() > page.bottom() + 0.5f) {
                std::printf("  (hors de la page : %s)\n", b->id().c_str());
                return false;
            }
        }
        if (!buttonsInside(*c, page)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    installMetrics();
    TextRenderer r;

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Editions : pas encore de registre\n");
    {
        Fixture fx(source, "donnees-vide");
        HostRig rig(fx);
        gui::ShellHost h = rig.host();
        auto page = gui::makeEditionsPage(h);
        rig.refreshers.push_back([&] { page->refresh(); });
        check(page->title() == "Offres", "le titre de la page (1.42.0 : les offres)");
        std::string t = paint(*page, r);
        check(has(t, "Pas encore de registre"), "sans registre : la page le dit");
        check(has(t, "docs/LICENCES.md") && has(t, "registre-exemple.json"), "le format et l'exemple sont nommes");
        check(has(t, "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9"), "sans cle : ou la creer");
        check(shown(*page, "editions.premier") && !enabledBtn(*page, "editions.premier"), "sans cle : le premier registre ne se signe pas (bouton grise)");
        check(!shown(*page, "editions.signer") && !shown(*page, "editions.table"), "sans registre : ni table ni Signer");
        check(fx.ws->createKey("essai application", true).ok, "la cle d'essai");
        page->refresh();
        check(enabledBtn(*page, "editions.premier"), "avec la cle : le bouton s'active");
        rig.picks.push_back("");
        check(click(*page, "editions.premier") && rig.asked.size() == 1 && rig.reports.empty(), "annule dans l'explorateur : rien");
        check(!rig.asked.empty() && !rig.asked.back().filters.empty() && rig.asked.back().filters.front().second == "json", "on choisit un .json");
        // Un travail lent en cours : refuse.
        check(startSlow(rig, h), "un travail lent demarre");
        rig.picks.push_back(source + "/tests/fixtures/licences/registre-exemple.json");
        (void)click(*page, "editions.premier");
        check(!fx.ws->hasRegistry() && !rig.flashes.empty(), "pendant un travail lent : refuse (et dit)");
        check(rig.finishSlow(), "le travail lent finit");
        rig.slowOnThread = false;
        rig.picks.clear();
        rig.picks.push_back(source + "/tests/fixtures/licences/registre-exemple.json");
        check(click(*page, "editions.premier"), "Signer le premier registre depuis un fichier...");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "Registre n\xC2\xB0 318"), "le registre n. 318 est signe");
        check(fx.ws->hasRegistry() && fx.ws->registry().editions.size() == 6, "rang\xC3\xA9 dans l'archive (6 editions)");
        t = paint(*page, r);
        check(rows(*page, "editions.table") == 6 && shown(*page, "editions.table"), "la table des editions apparait");
        check(!shown(*page, "editions.premier"), "le bouton du premier registre disparait");
    }

    Fixture fx(source, "donnees");
    check(fx.ready(), "le poste d'essai est pret (cle d'essai, registre n. 318)");
    HostRig rig(fx);
    gui::ShellHost h = rig.host();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Editions : le registre d'exemple, modifier, signer\n");
    auto editions = gui::makeEditionsPage(h);
    auto features = gui::makeFeaturesPage(h);
    rig.refreshers.push_back([&] { editions->refresh(); });
    rig.refreshers.push_back([&] { features->refresh(); });
    {
        auto& p = *editions;
        std::string t = paint(p, r);
        check(rows(p, "editions.table") == 6, "6 editions");
        check(cell(p, "editions.table", 0, 0) == "FREE" && cell(p, "editions.table", 2, 0) == "PROFESSIONAL" && cell(p, "editions.table", 5, 0) == "CUSTOM",
              "dans l'ordre du registre");
        check(table(p, "editions.table")->model()->cellStyle(0, 0).badge == "sans licence", "FREE : l'edition sans licence est marquee");
        check(cell(p, "editions.table", 2, 3) == "5" && cell(p, "editions.table", 2, 4) == "365 j" && cell(p, "editions.table", 2, 6) == "18",
              "PROFESSIONAL : 5 PC, 365 j, 18 fonctionnalites");
        check(cell(p, "editions.table", 0, 4) == "sans fin", "FREE : sans fin");
        check(has(t, "Registre n\xC2\xB0 318") && has(t, "\xC3\xA0 jour"), "le numero du registre, a jour");
        check(has(t, "prochaine"), "l'explication : le registre part avec la prochaine licence");
        // 1.47.0 : a cote de << Supprimer l'offre... >>, la forme courte quand la longue ne tient pas (jamais par-dessus le bouton)
        check(has(t, "Offre FREE") && (has(t, "l'offre sans licence") || has(t, "sans licence\n")), "le panneau : FREE, l'offre sans licence (1.42.0 : les offres)");
        check(!enabledBtn(p, "editions.signer") && !enabledBtn(p, "editions.annuler"), "rien a signer : Signer et Annuler grises");
        check(btnText(p, "editions.signer") == "Signer le registre n\xC2\xB0 319", "le bouton dit le numero suivant");

        check(selectRow(p, "editions.table", 0, "PROFESSIONAL"), "choisir PROFESSIONAL");
        check(fieldText(p, "editions.libelle") == "PROFESSIONAL" && fieldText(p, "editions.pc") == "5" && fieldText(p, "editions.duree") == "365"
                  && fieldText(p, "editions.grace") == "14",
              "le panneau montre ses valeurs");
        check(type(p, "editions.libelle", "Professionnel"), "changer le libelle");
        check(cell(p, "editions.table", 2, 1) == "Professionnel", "la table suit");
        check(table(p, "editions.table")->model()->cellStyle(2, 1).badge == "modifi\xC3\xA9" "e", "la ligne est marquee modifiee");
        const int adv = rowOf(p, "editions.fonctionnalites", 1, "ai_advanced_models");
        check(adv > 0 && table(p, "editions.fonctionnalites")->model()->cellStyle(static_cast<ui::RowIndex>(adv), 0).check == 0, "ai_advanced_models : pas accordee");
        table(p, "editions.fonctionnalites")->checkClicked->emit(static_cast<ui::RowIndex>(adv));
        check(table(p, "editions.fonctionnalites")->model()->cellStyle(static_cast<ui::RowIndex>(adv), 0).check == 1, "cochee");
        check(cell(p, "editions.table", 2, 6) == "19", "19 fonctionnalites");
        const int head = rowOf(p, "editions.fonctionnalites", 0, "IA");
        check(head >= 0 && table(p, "editions.fonctionnalites")->model()->cellStyle(static_cast<ui::RowIndex>(head), 0).spanRow, "groupees par produit (un titre IA)");
        t = paint(p, r);
        check(has(t, "2 modifications non sign\xC3\xA9" "es") || has(t, "1 modification non sign\xC3\xA9" "e"), "modifications non signees");
        check(fx.ws->registry().number == 318, "rien n'est signe tant qu'on ne signe pas");
        check(enabledBtn(p, "editions.signer") && enabledBtn(p, "editions.annuler"), "Signer et Annuler s'activent");
        // La page Fonctionnalites voit le meme brouillon.
        features->refresh();
        check(has(paint(*features, r), "non sign\xC3\xA9"), "la page Fonctionnalites voit le meme brouillon");
        check(click(p, "editions.signer"), "Signer le registre n. 319");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "Registre n\xC2\xB0 319"), "signe : n. 319");
        check(fx.ws->registry().number == 319, "l'archive a le n. 319");
        const auto* pro = fx.ws->registry().edition("PROFESSIONAL");
        check(pro && pro->label == "Professionnel" && pro->features.size() == 19, "le libelle et la fonctionnalite sont dans le registre signe");
        bool changeLine = false;
        if (rig.last())
            for (const auto& d : rig.last()->details) changeLine = changeLine || (has(d, "PROFESSIONAL") && has(d, "+ai_advanced_models"));
        check(changeLine, "le resultat dit ce qui a change");
        t = paint(p, r);
        check(has(t, "Registre n\xC2\xB0 319") && has(t, "\xC3\xA0 jour") && !enabledBtn(p, "editions.signer"), "a jour apres la signature");
        check(btnText(p, "editions.signer") == "Signer le registre n\xC2\xB0 320", "le suivant sera le n. 320");
        check(fieldText(p, "editions.libelle") == "Professionnel", "le panneau garde l'edition choisie");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Editions : refus et annulation\n");
    {
        auto& p = *editions;
        check(selectRow(p, "editions.table", 0, "PERSONAL"), "choisir PERSONAL");
        check(type(p, "editions.pc", "0"), "0 PC");
        std::string t = paint(p, r);
        check(has(t, "PC au plus"), "0 PC : refuse sur la page");
        check(type(p, "editions.libelle", "Personnel"), "et un libelle change");
        check(enabledBtn(p, "editions.signer") && click(p, "editions.signer"), "Signer");
        check(fx.ws->registry().number == 319 && has(paint(p, r), "Corrige d'abord"), "un champ refuse : rien n'est signe");
        check(type(p, "editions.pc", "abc"), "des lettres");
        check(has(paint(p, r), "nombre entier"), "des lettres : refuse");
        check(type(p, "editions.pc", "3"), "3 PC");
        check(cell(p, "editions.table", 1, 3) == "3" && !has(paint(p, r), "Corrige"), "3 PC : accepte");
        check(type(p, "editions.libelle", "   "), "un libelle vide");
        check(has(paint(p, r), "ne peut pas \xC3\xAAtre vide") && cell(p, "editions.table", 1, 1) == "Personnel", "vide : refuse, l'ancien reste");
        check(type(p, "editions.libelle", "Personnel"), "le libelle revient");
        // L'edition sans licence ne se retire pas.
        check(selectRow(p, "editions.table", 0, "FREE") && setCheck(p, "editions.retiree", true), "retirer FREE");
        check(!find<ui::Checkbox>(p, "editions.retiree")->isChecked() && has(paint(p, r), "ne se retire pas"), "FREE : refuse, la case se decoche");
        check(!fx.ws->registry().edition("FREE")->retired && !gui::Workspace(fx.ws->folder()).registry().edition("FREE")->retired, "FREE n'est pas retiree");
        // Annuler les modifications.
        check(click(p, "editions.annuler"), "Annuler les modifications");
        check(cell(p, "editions.table", 1, 1) == "PERSONAL" && cell(p, "editions.table", 1, 3) == "2", "le brouillon repart du registre signe");
        check(!enabledBtn(p, "editions.signer") && has(paint(p, r), "\xC3\xA0 jour"), "plus rien a signer");
        // Busy : Signer refuse pendant un travail lent.
        check(type(p, "editions.libelle", "FREE !"), "une modification");
        check(startSlow(rig, h), "un travail lent");
        const std::size_t before = rig.reports.size();
        (void)click(p, "editions.signer");
        check(fx.ws->registry().number == 319 && rig.reports.size() == before && !rig.flashes.empty(), "pendant un travail lent : Signer refuse");
        check(rig.finishSlow(), "le travail lent finit");
        rig.slowOnThread = false;
        check(click(p, "editions.annuler"), "annule");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Editions : nouvelle edition, retirer\n");
    {
        auto& p = *editions;
        check(!shown(p, "editions.nouvelle.id"), "le formulaire est ferme");
        check(click(p, "editions.nouvelle") && shown(p, "editions.nouvelle.id") && shown(p, "editions.nouvelle.creer"), "Nouvelle edition... ouvre le formulaire");
        check(has(paint(p, r), "MAJUSCULES"), "la regle de l'identifiant est dite");
        check(type(p, "editions.nouvelle.id", "pro plus") && click(p, "editions.nouvelle.creer"), "un identifiant en minuscules");
        check(rows(p, "editions.table") == 6 && has(paint(p, r), "n'est pas un identifiant"), "refuse");
        check(type(p, "editions.nouvelle.id", "PERSONAL") && click(p, "editions.nouvelle.creer"), "un identifiant deja pris");
        check(rows(p, "editions.table") == 6 && has(paint(p, r), "existe d\xC3\xA9j\xC3\xA0"), "refuse : existe deja");
        check(type(p, "editions.nouvelle.id", "") && click(p, "editions.nouvelle.creer") && rows(p, "editions.table") == 6, "vide : refuse");
        check(type(p, "editions.nouvelle.id", "PRO_PLUS") && type(p, "editions.nouvelle.libelle", "Professionnel +") && click(p, "editions.nouvelle.creer"),
              "PRO_PLUS");
        check(rows(p, "editions.table") == 7 && cell(p, "editions.table", 6, 0) == "PRO_PLUS", "la 7e edition");
        check(!shown(p, "editions.nouvelle.id"), "le formulaire se ferme");
        check(fieldText(p, "editions.libelle") == "Professionnel +" && fieldText(p, "editions.pc") == "1" && fieldText(p, "editions.duree") == "365",
              "elle est choisie, ses valeurs par defaut");
        check(table(p, "editions.table")->model()->cellStyle(6, 1).badge == "nouvelle", "marquee nouvelle");
        const int ed = rowOf(p, "editions.fonctionnalites", 1, "api_editor");
        table(p, "editions.fonctionnalites")->checkClicked->emit(static_cast<ui::RowIndex>(ed));
        check(type(p, "editions.duree", "0") && pickDrop(p, "editions.type", "perpetual"), "sans fin, perpetuelle");
        check(click(p, "editions.signer") && fx.ws->registry().number == 320, "signe : n. 320");
        const auto* pp = fx.ws->registry().edition("PRO_PLUS");
        check(pp && pp->label == "Professionnel +" && pp->maxDevices == 1 && pp->durationDays == 0 && pp->type == licensing::LicenseType::Perpetual
                  && pp->features.size() == 1 && pp->features.front().id == "api_editor",
              "PRO_PLUS est dans le registre signe");
        // Retirer (jamais supprimer).
        check(selectRow(p, "editions.table", 0, "CUSTOM") && setCheck(p, "editions.retiree", true), "retirer CUSTOM");
        check(cell(p, "editions.table", 5, 7) == "retir\xC3\xA9" "e", "la table le montre");
        check(click(p, "editions.signer") && fx.ws->registry().number == 321, "signe : n. 321");
        check(fx.ws->registry().edition("CUSTOM") && fx.ws->registry().edition("CUSTOM")->retired && fx.ws->registry().editions.size() == 7,
              "CUSTOM est retiree, pas supprimee");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Fonctionnalites\n");
    {
        auto& p = *features;
        features->refresh();
        check(features->title() == "Fonctionnalit\xC3\xA9s", "le titre");
        std::string t = paint(p, r);
        check(rows(p, "fonctionnalites.table") == 21, "21 fonctionnalites");
        const int hmi = rowOf(p, "fonctionnalites.table", 0, "hmi_editor");
        check(hmi >= 0 && cell(p, "fonctionnalites.table", static_cast<std::size_t>(hmi), 3) == "max_views", "hmi_editor : sa limite max_views");
        check(cell(p, "fonctionnalites.table", static_cast<std::size_t>(rowOf(p, "fonctionnalites.table", 0, "api_editor")), 5) == "6",
              "api_editor : dans 6 editions (avec PRO_PLUS)");
        check(selectRow(p, "fonctionnalites.table", 0, "hmi_editor"), "choisir hmi_editor");
        check(fieldText(p, "fonctionnalites.libelle") == "\xC3\x89" "diteur de vues" && fieldText(p, "fonctionnalites.produit") == "IHM Studio", "le panneau");
        check(rows(p, "fonctionnalites.limites") == 1 && cell(p, "fonctionnalites.limites", 0, 0) == "max_views", "ses limites");
        t = paint(p, r);
        check(has(t, "Accord\xC3\xA9" "e par"), "les editions qui l'accordent");
        // Nouvelle fonctionnalite.
        check(click(p, "fonctionnalites.nouvelle") && shown(p, "fonctionnalites.nouvelle.id"), "Nouvelle fonctionnalite...");
        check(type(p, "fonctionnalites.nouvelle.id", "Rapports") && type(p, "fonctionnalites.nouvelle.produit", "IHM Studio")
                  && type(p, "fonctionnalites.nouvelle.libelle", "Rapports") && click(p, "fonctionnalites.nouvelle.creer"),
              "un identifiant en majuscules");
        check(rows(p, "fonctionnalites.table") == 21 && has(paint(p, r), "n'est pas un identifiant"), "refuse");
        check(type(p, "fonctionnalites.nouvelle.id", "hmi_editor") && click(p, "fonctionnalites.nouvelle.creer") && has(paint(p, r), "existe d\xC3\xA9j\xC3\xA0"),
              "deja pris : refuse");
        check(type(p, "fonctionnalites.nouvelle.id", "hmi_reports") && type(p, "fonctionnalites.nouvelle.produit", "") && click(p, "fonctionnalites.nouvelle.creer")
                  && rows(p, "fonctionnalites.table") == 21,
              "sans produit : refuse");
        check(type(p, "fonctionnalites.nouvelle.produit", "IHM Studio") && click(p, "fonctionnalites.nouvelle.creer"), "hmi_reports");
        check(rows(p, "fonctionnalites.table") == 22 && fieldText(p, "fonctionnalites.libelle") == "Rapports", "la 22e, choisie");
        // Une limite.
        check(type(p, "fonctionnalites.limite.nom", "Max Reports") && click(p, "fonctionnalites.limite.ajouter") && rows(p, "fonctionnalites.limites") == 0,
              "un nom de limite non valide : refuse");
        check(type(p, "fonctionnalites.limite.nom", "max_reports") && type(p, "fonctionnalites.limite.libelle", "Rapports au plus")
                  && click(p, "fonctionnalites.limite.ajouter"),
              "ajouter la limite max_reports");
        check(rows(p, "fonctionnalites.limites") == 1 && cell(p, "fonctionnalites.limites", 0, 1) == "Rapports au plus", "la limite est la");
        check(type(p, "fonctionnalites.limite.nom", "max_reports") && type(p, "fonctionnalites.limite.libelle", "Rapports par mois")
                  && click(p, "fonctionnalites.limite.ajouter") && cell(p, "fonctionnalites.limites", 0, 1) == "Rapports par mois",
              "la renommer");
        check(click(p, "fonctionnalites.signer") && fx.ws->registry().number == 322, "signe : n. 322");
        const auto* rep = fx.ws->registry().feature("hmi_reports");
        check(rep && rep->product == "IHM Studio" && rep->label == "Rapports" && rep->limits.size() == 1 && rep->limits[0].id == "max_reports"
                  && rep->limits[0].label == "Rapports par mois",
              "hmi_reports et sa limite sont dans le registre signe");
        // Elle apparait dans la page Editions (sous IHM Studio).
        editions->refresh();
        check(rowOf(*editions, "editions.fonctionnalites", 1, "hmi_reports") > 0, "la page Editions la propose");
        // Retirer une limite : choisir, retirer.
        check(selectRow(p, "fonctionnalites.limites", 0, "max_reports") && click(p, "fonctionnalites.limite.retirer"), "retirer la limite");
        check(rows(p, "fonctionnalites.limites") == 0 && fx.ws->registry().feature("hmi_reports") && fx.ws->registry().feature("hmi_reports")->limits.size() == 1, "retiree du brouillon (pas encore signee)");
        check(click(p, "fonctionnalites.annuler") && rows(p, "fonctionnalites.limites") == 1, "annuler : elle revient");
        // Retirer la fonctionnalite.
        check(setCheck(p, "fonctionnalites.retiree", true) && click(p, "fonctionnalites.signer"), "retirer hmi_reports, signer");
        check(fx.ws->registry().number == 323 && fx.ws->registry().feature("hmi_reports") && fx.ws->registry().feature("hmi_reports")->retired && fx.ws->registry().features.size() == 22,
              "retiree, pas supprimee");
        // La page Editions refuse de l'accorder.
        editions->refresh();
        check(selectRow(*editions, "editions.table", 0, "PERSONAL"), "PERSONAL");
        const int rr = rowOf(*editions, "editions.fonctionnalites", 1, "hmi_reports");
        table(*editions, "editions.fonctionnalites")->checkClicked->emit(static_cast<ui::RowIndex>(rr));
        check(has(paint(*editions, r), "est retir\xC3\xA9" "e") && !has(paint(*editions, r), "non sign\xC3\xA9"), "une fonctionnalite retiree ne s'accorde pas");
        // Un registre signe ailleurs pendant des modifications : la signature est refusee.
        check(type(p, "fonctionnalites.libelle", "Rapports (ancien)"), "une modification en cours");
        check(fx.ws->signRegistry(fx.ws->registry()).ok && fx.ws->registry().number == 324, "le registre est signe ailleurs (n. 324)");
        features->refresh();
        check(has(paint(p, r), "a \xC3\xA9t\xC3\xA9 sign\xC3\xA9 depuis"), "la page le dit");
        check(click(p, "fonctionnalites.signer") && rig.last() && !rig.last()->ok && has(rig.last()->message, "annule-les"), "Signer : refuse");
        check(fx.ws->registry().number == 324, "rien n'est ecrase");
        check(click(p, "fonctionnalites.annuler") && fieldText(p, "fonctionnalites.libelle") == "Rapports" && has(paint(p, r), "Registre n\xC2\xB0 324"),
              "annuler : le brouillon repart du n. 324");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Activations\n");
    const std::string reqA = fx.request("pcA", "Poste essai A");
    const std::string reqB = fx.request("pcB", "Poste essai B");
    const std::string reqC = fx.request("pcC", "Poste essai C");
    auto activations = gui::makeActivationsPage(h);
    rig.refreshers.push_back([&] { activations->refresh(); });
    {
        auto& p = *activations;
        check(p.title() == "Activations", "le titre");
        std::string t = paint(p, r);
        check(rows(p, "activations.table") == 0 && has(t, "Aucun PC connu"), "vide : la page le dit");
        check(has(t, "mat\xC3\xA9riel") && has(t, "service"), "l'explication : seulement ce que les clients envoient");
        g_now += 60;
        rig.picks.push_back(reqA);
        check(click(p, "activations.noter"), "Noter des demandes...");
        check(!rig.asked.empty() && rig.asked.back().filters.front().second == "xpgreq", "on choisit un .xpgreq");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "1 demande not\xC3\xA9" "e"), "1 demande notee");
        check(rows(p, "activations.table") == 1 && cell(p, "activations.table", 0, 0) == "Poste essai A" && cell(p, "activations.table", 0, 9) == "demande",
              "le PC, en demande");
        g_now += 60;
        check(p.dropFiles({reqB}), "glisser une demande : prise par la page");
        check(rows(p, "activations.table") == 2, "2 PC");
        check(!p.dropFiles({fx.root.string() + "/x.xpglic"}) && !p.dropFiles({reqC, fx.root.string() + "/a.txt"}) && !p.dropFiles({}),
              "autre chose qu'une demande : laisse a la coquille");
        check(rows(p, "activations.table") == 2, "rien de note pour un melange");
        g_now += 60;
        check(fx.licence("XPG-2026-00001", "Client d'essai Alpha", "PROFESSIONAL", {reqA}).ok, "une licence signee pour le PC A");
        p.refresh();
        const int a = rowOf(p, "activations.table", 0, "Poste essai A");
        check(rows(p, "activations.table") == 2 && a >= 0 && cell(p, "activations.table", static_cast<std::size_t>(a), 9) == "active"
                  && cell(p, "activations.table", static_cast<std::size_t>(a), 2) == "Client d'essai Alpha"
                  && cell(p, "activations.table", static_cast<std::size_t>(a), 3) == "XPG-2026-00001"
                  && cell(p, "activations.table", static_cast<std::size_t>(a), 4) == "PROFESSIONAL",
              "A : active, son client, sa licence, son edition");
        check(!cell(p, "activations.table", static_cast<std::size_t>(a), 1).empty() && has(cell(p, "activations.table", static_cast<std::size_t>(a), 5), "IHM Studio"),
              "son empreinte, son logiciel");
        t = paint(p, r);
        check(has(t, "2 PC, 1 licence, 1 client"), "les totaux");
        // 1.47.0 : l'etat est une puce de la barre << Filtrer et chercher >> (le champ 9 : Etat), plus une liste deroulante.
        auto* bar = find<ui::FilterBar>(p, "activations");
        const auto pickState = [&](const std::string& label) {
            if (!bar) return false;
            if (label.empty()) bar->clearField(9);
            else bar->setValues(9, {label});
            return true;
        };
        check(pickState("active") && rows(p, "activations.table") == 1 && cell(p, "activations.table", 0, 0) == "Poste essai A",
              "filtre : actives");
        check(has(paint(p, r), "sur 2 PC en tout"), "le total dit le filtre");
        check(pickState("demande") && rows(p, "activations.table") == 1 && cell(p, "activations.table", 0, 0) == "Poste essai B",
              "filtre : demandes");
        check(pickState("r\xC3\xA9voqu\xC3\xA9" "e") && rows(p, "activations.table") == 0 && has(paint(p, r), "ne correspond"), "filtre : revoquees (aucune)");
        check(pickState(""), "tous les etats");
        check(type(p, "activations.recherche", "\"essai Alpha\"") && rows(p, "activations.table") == 1, "chercher le client");
        check(type(p, "activations.recherche", "\"essai B\"") && rows(p, "activations.table") == 1 && cell(p, "activations.table", 0, 0) == "Poste essai B", "chercher un PC");
        // Exporter ce qui est montre.
        rig.picks.push_back(U(fx.root / "sortie" / "activations-b"));
        check(click(p, "activations.exporter"), "Exporter pour Excel...");
        check(rig.asked.back().save && rig.last() && rig.last()->ok, "ecrit");
        const std::string csv = readFile(fx.root / "sortie" / "activations-b.csv");
        check(csv.rfind("\xEF\xBB\xBF", 0) == 0 && has(csv, "Poste essai B") && !has(csv, "Poste essai A") && has(csv, "\r\n") && has(csv, ";"),
              "le CSV : BOM, ; , CRLF, seulement les PC montres (.csv ajoute)");
        check(type(p, "activations.recherche", ""), "recherche videe");
        check(click(p, "activations.dossier") && !rig.openedFolders.empty() && rig.openedFolders.back() == fx.ws->folder(), "Ouvrir le registre des activations");
        // Pendant un travail lent : une demande glissee est refusee (et dite).
        check(startSlow(rig, h), "un travail lent");
        const std::size_t flashes = rig.flashes.size();
        check(p.dropFiles({reqC}) && rig.flashes.size() == flashes + 1, "glissee pendant un travail lent : refusee, dit");
        check(rig.finishSlow(), "fini");
        rig.slowOnThread = false;
        check(rows(p, "activations.table") == 2, "rien de note pendant le travail lent");
        g_now += 60;
        check(p.dropFiles({reqC}) && rows(p, "activations.table") == 3, "apres : notee");
        // Une ligne illisible dans activations.jsonl : comptee et dite.
        {
            std::ofstream out(fs::path(license_admin::activationsPath(fx.ws->folder())), std::ios::binary | std::ios::app);
            out << "{pas du json}\n";
        }
        fx.ws->reload();
        p.refresh();
        check(fx.ws->skippedLines() == 1 && has(paint(p, r), "1 ligne illisible ignor\xC3\xA9" "e"), "une ligne illisible : dite");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Clients\n");
    g_now += 60;
    const std::string reqD = fx.request("pcD", "Poste essai D", "XPG-2026-00001");
    check(fx.ws->recordRequests({reqD}).ok, "une demande pour la licence d'Alpha");
    auto clients = gui::makeClientsPage(h);
    rig.refreshers.push_back([&] { clients->refresh(); });
    {
        auto& p = *clients;
        check(p.title() == "Clients", "le titre");
        std::string t = paint(p, r);
        // 1.42.0 : le deballage en arbre - replie, une ligne par client
        check(rows(p, "clients.table") == 1 && cell(p, "clients.table", 0, 0) == "Client d'essai Alpha", "le client d'essai (replie : une ligne)");
        check(has(cell(p, "clients.table", 0, 1), "1 licence") && has(cell(p, "clients.table", 0, 1), "active") && cell(p, "clients.table", 0, 3) == "1",
              "1 licence active, 1 PC");
        check(!cell(p, "clients.table", 0, 4).empty(), "sa derniere activite");
        check(has(t, "D\xC3\xA9plie-le"), "l'explication : deplier le client");
        check(has(t, "1 demande en attente"), "la fiche : la demande en attente");
        check(click(p, "clients.deplier") && rowOf(p, "clients.table", 0, "XPG-2026-00001") >= 0 && rowOf(p, "clients.table", 0, "PC (1)") >= 0 &&
                  rowOf(p, "clients.table", 0, "Poste essai A") >= 0 && rowOf(p, "clients.table", 0, "Demande : Poste essai D") >= 0,
              "Tout deplier : la licence, ses PC, la demande en attente");
        {
            const int lr = rowOf(p, "clients.table", 0, "XPG-2026-00001");
            check(lr >= 0 && has(cell(p, "clients.table", static_cast<std::size_t>(lr), 1), "PROFESSIONAL") &&
                      has(cell(p, "clients.table", static_cast<std::size_t>(lr), 1), "active"),
                  "la ligne de la licence : son offre, son etat");
        }
        check(click(p, "clients.replier") && rows(p, "clients.table") == 1, "Tout replier : le client seul");
        if (auto* tv = table(p, "clients.table")) tv->expanderClicked->emit(0);
        check(rows(p, "clients.table") > 1 && rowOf(p, "clients.table", 0, "XPG-2026-00001") >= 0 && rowOf(p, "clients.table", 0, "Poste essai A") < 0,
              "la fleche du client : ses licences (leurs PC restent replies)");
        check(!click(p, "clients.supprimer") && has(paint(p, r), "Une licence en cours"), "Supprimer le client : grise tant qu'une licence court (dit pourquoi)");
        const std::size_t reportsBefore = rig.reports.size();
        check(click(p, "clients.actualiser") && rig.reports.size() == reportsBefore + 1 && has(rig.reports.back().message, "Clients relus"),
              "Actualiser : tout est relu (le resultat le dit)");
        check(click(p, "clients.nouvelle-licence") && rig.dialogs.size() == 1 && rig.dialogs.back() != nullptr, "Nouvelle licence pour ce client... ouvre l'assistant");
        if (!rig.dialogs.empty() && rig.dialogs.back() && rig.dialogs.back()->widgetRoot()) {
            TextRenderer dr;
            std::printf("  (l'assistant montre le client : %s)\n", has(paint(*rig.dialogs.back()->widgetRoot(), dr), "Client d'essai Alpha") ? "oui" : "non");
        }
        check(click(p, "clients.voir-licences") && !rig.navigations.empty() && rig.navigations.back() == "licences", "Voir ses licences : la page Licences");
        check(type(p, "clients.recherche", "zzz") && rows(p, "clients.table") == 0 && has(paint(p, r), "Aucun client ne correspond"), "chercher : aucun");
        check(!click(p, "clients.nouvelle-licence") && !click(p, "clients.voir-licences"), "sans client choisi : boutons grises");
        check(type(p, "clients.recherche", "ALPHA") && rows(p, "clients.table") >= 1 && cell(p, "clients.table", 0, 0) == "Client d'essai Alpha",
              "chercher sans tenir compte des majuscules");
        check(type(p, "clients.recherche", "Poste essai A") && rows(p, "clients.table") >= 1 && cell(p, "clients.table", 0, 0) == "Client d'essai Alpha",
              "chercher un PC : son client");
        Fixture empty(source, "donnees-sans-client");
        HostRig er(empty);
        auto ep = gui::makeClientsPage(er.host());
        check(rows(*ep, "clients.table") == 0 && has(paint(*ep, r), "Pas encore de client"), "sans licence : pas encore de client");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Historique\n");
    {
        // Une extension, deux licences revoquees (la liste re-signee repete la premiere).
        g_now += 60;
        check(fx.licence("XPG-2026-00002", "Client d'essai Beta", "PERSONAL", {reqB}).ok, "une licence pour Beta");
        g_now += 60;
        gui::LicenceDraft ext;
        ext.licenseId = "XPG-2026-00002";
        ext.customer = "Client d'essai Beta";
        ext.edition = "PERSONAL";
        ext.requestPaths = {reqC};
        ext.extend = true;
        const auto er = fx.ws->signLicence(ext, U(fx.root / "sortie" / "XPG-2026-00002-b.xpglic"));
        check(er.ok, "etendue au PC C");
        if (!er.ok) std::printf("  (%s)\n", er.text().c_str());
        g_now += 60;
        check(fx.ws->revoke("XPG-2026-00001", "", "Fin de contrat (essai)", false).ok, "Alpha revoquee");
        g_now += 60;
        check(fx.ws->revoke("XPG-2026-00002", "", "Essai", false).ok, "Beta revoquee");
    }
    auto history = gui::makeHistoryPage(h);
    rig.refreshers.push_back([&] { history->refresh(); });
    {
        auto& p = *history;
        check(p.title() == "Historique", "le titre");
        std::string t = paint(p, r);
        const std::size_t n = rows(p, "historique.table");
        // demandes A, B, C(noted), D ; licence A ; licence B ; extension B (B + C) ; revocations : A, puis A et B (A repetee)
        check(n == 10, "10 lignes (une revocation repetee masquee) : " + std::to_string(n));
        check(has(t, "1 r\xC3\xA9vocation r\xC3\xA9p\xC3\xA9t\xC3\xA9" "e masqu\xC3\xA9" "e"), "la repetition masquee est dite");
        check(cell(p, "historique.table", 0, 1) == "R\xC3\xA9vocation" && cell(p, "historique.table", 0, 2) == "XPG-2026-00002", "la plus recente d'abord");
        check(cell(p, "historique.table", n - 1, 1) == "Demande" && cell(p, "historique.table", n - 1, 4) == "Poste essai A", "la plus ancienne a la fin");
        check(cell(p, "historique.table", 0, 3) == "Client d'essai Beta", "une revocation : le client de sa licence");
        check(has(cell(p, "historique.table", 0, 5), "raison : Essai") && cell(p, "historique.table", 0, 4) == "toute la licence", "son detail");
        check(pickType(p, "licence") && rows(p, "historique.table") == 2, "filtre : licences (A, B)");
        const int la = rowOf(p, "historique.table", 2, "XPG-2026-00001");
        check(la >= 0 && has(cell(p, "historique.table", static_cast<std::size_t>(la), 5), "\xC3\xA9" "dition PROFESSIONAL")
                  && cell(p, "historique.table", static_cast<std::size_t>(la), 3) == "Client d'essai Alpha",
              "la licence d'Alpha : edition, client");
        check(pickType(p, "extension") && rows(p, "historique.table") == 2 && cell(p, "historique.table", 0, 1) == "Extension",
              "filtre : extensions (B garde, C ajoute)");
        check(pickType(p, "demande") && rows(p, "historique.table") == 4, "filtre : demandes");
        check(has(paint(p, r), "4 lignes sur 10"), "le compte dit le filtre");
        check(pickType(p, "revocation") && rows(p, "historique.table") == 2, "filtre : revocations");
        check(pickType(p, "") && type(p, "historique.recherche", "\"Poste essai D\"") && rows(p, "historique.table") == 1
                  && cell(p, "historique.table", 0, 1) == "Demande",
              "chercher un PC");
        check(type(p, "historique.recherche", "") && rows(p, "historique.table") == 10, "recherche videe");
        // Exporter : refuse pendant un travail lent.
        check(startSlow(rig, h), "un travail lent");
        const std::size_t nr = rig.reports.size(), nf = rig.flashes.size();
        rig.picks.push_back(U(fx.root / "sortie" / "historique-lent.csv"));
        (void)click(p, "historique.exporter");
        check(rig.reports.size() == nr && rig.flashes.size() == nf + 1 && !fs::exists(fx.root / "sortie" / "historique-lent.csv"),
              "pendant un travail lent : Exporter refuse (et dit)");
        check(rig.finishSlow(), "fini");
        rig.slowOnThread = false;
        rig.picks.clear();
        // Exporter.
        rig.picks.push_back(U(fx.root / "sortie" / "historique.csv"));
        check(click(p, "historique.exporter") && rig.last() && rig.last()->ok, "Exporter...");
        const std::string csv = readFile(fx.root / "sortie" / "historique.csv");
        check(csv.rfind("\xEF\xBB\xBF" "Date;Type;Licence;Client;PC;Empreinte;D\xC3\xA9tail\r\n", 0) == 0, "l'en-tete du CSV (BOM, ;, CRLF)");
        check(has(csv, "R\xC3\xA9vocation;XPG-2026-00002;Client d'essai Beta;toute la licence"), "une ligne de revocation");
        // Une formule dans une case : neutralisee (l'aide du CSV directement ; la fixture refuse les noms qui en sont).
        license_admin::ActivationRecord bad;
        bad.kind = "licence";
        bad.at = g_now;
        bad.licenseId = "@SUM(1)";
        bad.customer = "=CMD|' /C calc'!A0";
        bad.deviceName = "+Poste";
        bad.edition = "PRO";
        license_admin::ActivationRecord semi;
        semi.kind = "demande";
        semi.at = g_now;
        semi.deviceName = "Poste; \"essai\"";
        semi.fingerprint = "-";
        const std::string hc = gui::historyCsv({bad, semi});
        check(has(hc, ";'@SUM(1);'=CMD|' /C calc'!A0;'+Poste;") && !has(hc, ";=CMD") && !has(hc, ";+Poste"), "= + @ en tete : une apostrophe devant");
        check(has(hc, "\"Poste; \"\"essai\"\"\"") && has(hc, ";-;"), "; et \" entre guillemets ; - seul reste tel quel");
        // Par la page : une ligne ajoutee au registre des activations, exportee.
        check(license_admin::appendActivations(fx.ws->folder(), {bad}, nullptr), "une ligne a formule dans le registre");
        fx.ws->reload();
        p.refresh();
        rig.picks.push_back(U(fx.root / "sortie" / "historique-2.csv"));
        check(click(p, "historique.exporter"), "Exporter...");
        const std::string csv2 = readFile(fx.root / "sortie" / "historique-2.csv");
        check(has(csv2, "'=CMD") && !has(csv2, ";=CMD"), "par la page aussi : neutralisee");
        // 1.44.0 : un client supprime, puis restaure (le genre << client >>)
        license_admin::ActivationRecord gone;
        gone.kind = "client";
        gone.at = g_now + 10;
        gone.customer = "Client d'essai Gamma";
        gone.action = "supprime";
        gone.licences = {"XPG-2026-00008", "XPG-2026-00009"};
        gone.by = "admin+";
        license_admin::ActivationRecord back = gone;
        back.action = "restaure";
        back.at = g_now + 20;
        const std::string cc = gui::historyCsv({gone, back});
        check(has(cc, "Client supprim\xC3\xA9;;Client d'essai Gamma;;;Client supprim\xC3\xA9 : Client d'essai Gamma (2 licences), XPG-2026-00008, XPG-2026-00009, par admin+"),
              "le CSV : << Client supprime : <nom> (2 licences) >>");
        check(has(cc, "Client restaur\xC3\xA9;;Client d'essai Gamma;"), "le CSV : << Client restaure >>");
        check(license_admin::appendActivations(fx.ws->folder(), {gone, back}, nullptr), "deux lignes << client >> dans le registre");
        fx.ws->reload();
        p.refresh();
        check(pickType(p, "client") && rows(p, "historique.table") == 2 && cell(p, "historique.table", 0, 1) == "Client restaur\xC3\xA9" &&
                  cell(p, "historique.table", 1, 1) == "Client supprim\xC3\xA9" && cell(p, "historique.table", 1, 3) == "Client d'essai Gamma" &&
                  has(cell(p, "historique.table", 1, 5), "(2 licences)"),
              "filtre : clients (supprime, restaure)");
        check(pickType(p, ""), "le filtre remis");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- Disposition (1100x700 et 1400x900 ; la page seule : 860x600)\n");
    {
        std::vector<ui::Widget*> pages{editions.get(), features.get(), activations.get(), clients.get(), history.get()};
        check(click(*editions, "editions.nouvelle"), "le formulaire ouvert (le cas le plus haut)");
        for (const auto& [w, hgt] : std::vector<std::pair<float, float>>{{860.f, 600.f}, {1100.f, 700.f}, {1400.f, 900.f}})
            for (ui::Widget* pg : pages) {
                (void)paint(*pg, r, w, hgt);
                check(buttonsInside(*pg, {0.f, 0.f, w, hgt}), "les boutons tiennent dans " + pg->id() + " a " + std::to_string(static_cast<int>(w)));
            }
        check(click(*editions, "editions.nouvelle.fermer") && !shown(*editions, "editions.nouvelle.id"), "Fermer le formulaire");
        (void)paint(*editions, r, 860.f, 600.f);
        auto* ft = table(*editions, "editions.fonctionnalites");
        check(ft && ft->bounds().h >= 4.f * 24.f, "860x600 : la liste des fonctionnalites garde au moins 4 lignes (" + std::to_string(ft ? static_cast<int>(ft->bounds().h) : -1) + " px)");
        (void)paint(*features, r, 860.f, 600.f);
        auto* lt = table(*features, "fonctionnalites.limites");
        check(lt && lt->bounds().h >= 48.f, "860x600 : la table des limites reste visible");
    }

    return finish();
}
