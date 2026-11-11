// =============================================================================
//  tests/admin_gui_licences_test.cpp - 1.35.0 : l'application de l'editeur, la
//  page Licences, l'assistant << Nouvelle licence >> et la page Revocations
// -----------------------------------------------------------------------------
//  A blanc (TextRenderer, HostRig) sur un poste Admin d'essai (cle d'ESSAI,
//  registre d'exemple, PC fictifs) : l'assistant de bout en bout (deux demandes,
//  un client d'essai, PROFESSIONAL, 3 PC, 1 an, recapitulatif, signer vers un
//  fichier donne d'avance) ; ses refus (pas de cle, pas de registre, un
//  identifiant mal ecrit, une demande abimee, un travail en cours, l'enregistrement
//  annule) ; une licence perpetuelle aux fonctionnalites choisies ; une licence
//  etendue (un PC de plus) ; la page Licences (liste, filtres, recherche,
//  detail, exporter, revoquer toute la licence ou un PC, glisser des demandes) ;
//  la page Revocations (liste, revoquer, retirer, exporter). 1.35.0 : la
//  publication sur le service (un service d'essai ou rien n'ecoute : le refus
//  dit, rien d'invente), la licence EN LIGNE (l'assistant, le bail, le depot
//  refuse : aucun code montre ni ecrit), la page Licences d'une licence en ligne
//  (ses activations, Retirer ce PC, Nouveau code, Desactiver, Relever), le
//  dialogue du code (masque, Afficher, Copier). argv[1] : la racine du depot.
// =============================================================================
#include "../src/ui/widgets/FilterBar.hpp"   // 1.47.0 : la barre de filtres des listes
#include "admin_gui_harness.hpp"

#include "../src/ui/widgets/Containers.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <thread>
#include <vector>

using namespace admintest;
namespace online = license_admin::online;

namespace {

constexpr lic::Seconds kDay = 86400;

std::string paintDialog(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1280.f, 800.f);
}

std::string paintPage(ui::Widget& w, float width = 1100.f, float height = 700.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}

int rowOf(ui::TableView& t, std::size_t col, const std::string& text) {
    const auto& m = t.model();
    if (!m) return -1;
    for (std::size_t r = 0; r < m->rowCount(); ++r)
        if (m->cellText(static_cast<ui::RowIndex>(r), col) == text) return static_cast<int>(r);
    return -1;
}

std::string cell(ui::Widget& root, std::string_view id, int row, std::size_t col) {
    auto* t = find<ui::TableView>(root, id);
    if (!t || !t->model() || row < 0 || static_cast<std::size_t>(row) >= t->model()->rowCount()) return {};
    return t->model()->cellText(static_cast<ui::RowIndex>(row), col);
}

std::size_t rows(ui::Widget& root, std::string_view id) {
    auto* t = find<ui::TableView>(root, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}

bool shown(ui::Widget& root, std::string_view id) {
    ui::Widget* w = root.findById(id);
    return w && w->visible();
}

bool closed(menu::IMenu& m) { return m.widgetRoot() && !m.widgetRoot()->visible(); }

bool exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(std::u8string(path.begin(), path.end())), ec);
}

bool lastFlash(const HostRig& rig, const std::string& part) { return !rig.flashes.empty() && has(rig.flashes.back(), part); }

// Un travail lent qui dure un peu : la coquille est occupee.
void startBusy(HostRig& rig) {
    rig.bg.start("essai occupe", [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        return gui::OpResult::done("fini");
    });
}
void endBusy(HostRig& rig) {
    rig.bg.wait();
    (void)rig.bg.take();
}

// Un controle sur un texte dessine ; XPG_DUMP=1 : le texte est montre s'il echoue.
void checkText(bool ok, const std::string& what, const std::string& text) {
    check(ok, what);
    if (!ok && std::getenv("XPG_DUMP")) std::printf("---- %s ----\n%s----\n", what.c_str(), text.c_str());
}

// Avancer l'assistant d'une etape (Suivant doit etre pret).
bool next(ui::Widget& w) { return click(w, "wizard.suivant"); }

// Un code d'activation (XPG-XXXX-XXXX-XXXX-XXXX) ecrit dans un fichier du dossier : il ne doit y en avoir aucun.
bool codeWritten(const fs::path& dir) {
    static const std::regex code("XPG-[0-9A-Z]{4}-[0-9A-Z]{4}-[0-9A-Z]{4}-[0-9A-Z]{4}");
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        std::ifstream in(it->path(), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (std::regex_search(text, code)) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_licences_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- l'assistant refuse sans cle, puis sans registre\n");
    {
        Fixture fx(source, "lic-refus");
        HostRig rig(fx);
        auto h = rig.host();
        h.openDialog(gui::makeLicenceWizard(h, {}));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        const std::string t = paintDialog(d);
        check(has(t, "Impossible de signer"), "sans cle : l'assistant le dit");
        check(has(t, "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9"), "sans cle : il montre ou aller");
        check(!shown(w, "wizard.suivant") && !shown(w, "wizard.etape.demandes"), "sans cle : pas d'etapes");
        check(click(w, "wizard.aller") && !rig.navigations.empty() && rig.navigations.back() == "securite", "sans cle : Aller -> securite");
        check(closed(d), "sans cle : le dialogue se ferme en y allant");

        check(fx.ws->createKey("essai application", true).ok, "cle d'essai creee");
        h.openDialog(gui::makeLicenceWizard(h, {}));
        auto& d2 = *rig.dialogs.back();
        const std::string t2 = paintDialog(d2);
        check(has(t2, "registre") && has(t2, "\xC3\x89" "ditions"), "sans registre : l'assistant le dit");
        check(click(*d2.widgetRoot(), "wizard.aller") && rig.navigations.back() == "editions", "sans registre : Aller -> editions");
        check(fx.ws->licences().empty() && rig.reports.empty(), "rien n'est signe ni rapporte");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- l'assistant de bout en bout (2 demandes, PROFESSIONAL, 3 PC, 1 an)\n");
    Fixture fx(source, "lic-gui");
    check(fx.ready(), "poste d'essai pret (cle + registre)");
    const std::string reqA = fx.request("pcA", "Poste essai A");
    const std::string reqB = fx.request("pcB", "Poste essai B");
    HostRig rig(fx);
    auto h = rig.host();
    const std::string idAlpha = fx.ws->suggestLicenseId();
    std::string fileAlpha;
    {
        h.openDialog(gui::makeLicenceWizard(h, {}));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Nouvelle licence") && has(t, "1. Demandes des PC"), "etape 1 montree");
        check(has(t, "Client, identifiant") && has(t, "Signer"), "les six etapes a gauche");
        check(has(t, "Ajoute au moins une demande"), "etape 1 : ce qui manque est dit");
        check(!next(w), "etape 1 : Suivant grise sans demande");

        rig.picks = {reqA};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter...");
        check(!rig.asked.empty() && !rig.asked.back().save && !rig.asked.back().filters.empty() && rig.asked.back().filters.front().second == "xpgreq",
              "Ajouter... demande un .xpgreq");
        rig.picks = {reqB};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter... (2)");
        rig.picks = {reqA};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter... (la meme)");
        t = paintDialog(d);
        check(rows(w, "wizard.demandes") == 2, "deux demandes (la meme n'est pas reprise)");
        check(has(t, "Poste essai A") && has(t, "Poste essai B") && has(t, "valide"), "chaque demande lue : nom, etat");
        check(rowOf(*find<ui::TableView>(w, "wizard.demandes"), 3, "IHM Studio 1.33.0 (essai)") >= 0, "chaque demande lue : le logiciel");
        check(rowOf(*find<ui::TableView>(w, "wizard.demandes"), 4, gui::dateFr(g_now)) >= 0, "chaque demande lue : sa date");
        check(has(t, "d\xC3\xA9j\xC3\xA0 dans la liste"), "la demande en double est signalee");

        const std::string broken = U(fx.root / "abimee.xpgreq");
        {
            std::ofstream f(fx.root / "abimee.xpgreq", std::ios::binary);
            f << "ceci n'est pas une demande";
        }
        rig.picks = {broken};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter... (abimee)");
        t = paintDialog(d);
        check(rows(w, "wizard.demandes") == 3 && has(t, "refus\xC3\xA9" "e") && has(t, "\xC2\xAB abimee.xpgreq \xC2\xBB"), "la demande abimee est montree, refusee, sa raison dite");
        checkText(!next(w) && has(t, "Retire les demandes refus"), "une demande refusee bloque Suivant", t);
        auto* table = find<ui::TableView>(w, "wizard.demandes");
        table->selectModelRows({2});
        check(click(w, "wizard.demandes.retirer") && rows(w, "wizard.demandes") == 2, "Retirer");
        check(next(w), "etape 2");

        t = paintDialog(d);
        check(has(t, "2. Client et identifiant"), "etape 2 montree");
        check(find<ui::InputText>(w, "wizard.identifiant")->text() == idAlpha, "identifiant propose : le suivant de l'archive");
        check(!next(w), "etape 2 : sans client, Suivant grise");
        check(type(w, "wizard.client", "Client d'essai Alpha"), "client tape");
        check(type(w, "wizard.identifiant", "mauvais id!"), "identifiant mal ecrit");
        t = paintDialog(d);
        check(!next(w) && has(t, "identifiant n'est pas valide"), "identifiant mal ecrit : refuse");
        check(click(w, "wizard.identifiant.proposer") && find<ui::InputText>(w, "wizard.identifiant")->text() == idAlpha, "Proposer le suivant");
        check(next(w), "etape 3");

        t = paintDialog(d);
        check(has(t, "3. \xC3\x89" "dition") && has(t, "PROFESSIONAL") && has(t, "ENTERPRISE"), "etape 3 : les editions du registre");
        check(!next(w) && has(t, "Choisis une \xC3\xA9" "dition"), "etape 3 : une edition a choisir");
        auto* eds = find<ui::TableView>(w, "wizard.editions");
        const int pro = rowOf(*eds, 0, "PROFESSIONAL");
        check(pro >= 0, "PROFESSIONAL dans la liste");
        eds->selectModelRows({static_cast<ui::RowIndex>(pro)});
        check(find<ui::InputText>(w, "wizard.pc")->text() == "5", "les PC de l'edition (5)");
        check(find<ui::RadioButton>(w, "wizard.type.temporaire")->selected(), "le type de l'edition (temporaire)");
        check(!find<ui::RadioButton>(w, "wizard.type.flottante")->enabled(), "flottante : grisee (service en ligne)");
        check(find<ui::InputText>(w, "wizard.grace")->text() == "14", "la grace de l'edition (14 j)");
        check(type(w, "wizard.pc", "1"), "1 PC");
        t = paintDialog(d);
        check(!next(w) && has(t, "au moins 2"), "moins de PC que de demandes : refuse");
        check(type(w, "wizard.pc", "3"), "3 PC");
        check(click(w, "wizard.duree.30") && find<ui::InputText>(w, "wizard.duree")->text() == "30", "30 j");
        check(type(w, "wizard.fin", gui::dateFr(g_now + 400 * kDay)) && find<ui::InputText>(w, "wizard.duree")->text() == "400", "une date de fin -> les jours");
        check(type(w, "wizard.fin", "31/02/2027") && !next(w), "une date impossible : refusee");
        check(click(w, "wizard.duree.365") && find<ui::InputText>(w, "wizard.duree")->text() == "365", "1 an");
        check(find<ui::InputText>(w, "wizard.fin")->text() == gui::dateFr(g_now + 365 * kDay), "la date de fin suit la duree");
        check(next(w), "etape 4");

        t = paintDialog(d);
        check(has(t, "4. Fonctionnalit\xC3\xA9s") && has(t, "API Studio") && has(t, "IHM Studio"), "etape 4 : par logiciel");
        check(find<ui::Checkbox>(w, "wizard.f.api_editor")->isChecked() && !find<ui::Checkbox>(w, "wizard.f.ai_advanced_models")->isChecked(),
              "celles de l'edition cochees");
        check(has(t, "celles de l'\xC3\xA9" "dition PROFESSIONAL"), "le compte le dit");
        check(next(w), "etape 5");

        t = paintDialog(d);
        check(has(t, "5. R\xC3\xA9" "capitulatif") && has(t, "pr\xC3\xAAte \xC3\xA0 \xC3\xAAtre sign\xC3\xA9" "e"), "recapitulatif : la licence est prete");
        checkText(has(t, "Client d'essai Alpha") && has(t, "3 autoris\xC3\xA9s") && has(t, "365 j (1 an), jusqu'au " + gui::dateFr(g_now + 365 * kDay))
                  && has(t, "CL\xC3\x89 D'ESSAI"), "recapitulatif : les choix", t);
        check(next(w), "etape 6");

        t = paintDialog(d);
        check(has(t, "6. Signer") && shown(w, "wizard.signer") && !shown(w, "wizard.suivant"), "etape 6 : Signer et enregistrer...");
        rig.picks = {""};
        check(click(w, "wizard.signer"), "Signer (enregistrement annule)");
        t = paintDialog(d);
        check(fx.ws->licences().empty() && rig.reports.empty() && has(t, "Annul\xC3\xA9"), "annule : rien n'est signe");
        check(!rig.asked.empty() && rig.asked.back().save && has(rig.asked.back().start, idAlpha + ".xpglic"), "le nom propose : <ID>.xpglic");

        startBusy(rig);
        rig.picks = {U(fx.root / "jamais.xpglic")};
        check(click(w, "wizard.signer"), "Signer (occupe)");
        t = paintDialog(d);
        check(fx.ws->licences().empty() && has(t, "travail est en cours"), "un travail en cours : signer est refuse");
        endBusy(rig);
        rig.picks.clear();

        fs::create_directories(fx.root / "envoi");
        const std::string chosen = U(fx.root / "envoi" / "licence-alpha");   // sans extension : .xpglic s'ajoute
        fileAlpha = chosen + ".xpglic";
        rig.picks = {chosen};
        check(click(w, "wizard.signer"), "Signer");
        check(fx.ws->licences().size() == 1, "la licence est dans l'archive");
        const gui::LicenceEntry* l = fx.ws->licence(idAlpha);
        check(l != nullptr, "la licence a son identifiant");
        if (l) {
            check(l->licence.customer == "Client d'essai Alpha" && l->licence.edition == "PROFESSIONAL", "client et edition");
            check(l->licence.devices.size() == 2 && l->licence.maxDevices == 3, "2 PC lies sur 3");
            check(l->licence.type == lic::LicenseType::Temporary && l->licence.expiresAt == l->licence.issuedAt + 365 * kDay, "temporaire, 1 an");
            check(l->licence.offlineGraceDays == 14, "grace 14 j");
            const auto* e = fx.ws->registry().edition("PROFESSIONAL");
            check(e && l->licence.features.size() == e->features.size(), "les fonctionnalites de l'edition");
        }
        check(exists(fileAlpha), "le fichier .xpglic est ecrit");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, idAlpha) && rig.last()->output == fileAlpha, "le resultat est rapporte");
        t = paintDialog(d);
        check(has(t, "Licence sign\xC3\xA9" "e") && has(t, "Envoie ce fichier"), "le resultat est montre");
        check(shown(w, "wizard.dossier") && shown(w, "wizard.fermer") && !shown(w, "wizard.signer") && !shown(w, "wizard.annuler"), "apres : dossier, fermer");
        check(!fx.ws->hasServiceJob(), "le service pas regle : rien a publier");
        check(click(w, "wizard.dossier") && !rig.openedFolders.empty() && rig.openedFolders.back() == U(fx.root / "envoi"), "Ouvrir le dossier");
        check(click(w, "wizard.fermer") && closed(d), "Fermer");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une licence perpetuelle, fonctionnalites choisies, publiee (le service ne repond pas : refus dit)\n");
    const std::string reqC = fx.request("pcC", "Poste essai C");
    std::string idBeta;
    {
        // Un service d'essai sur ce poste ou rien n'ecoute : la publication part, et son refus est dit.
        check(fx.ws->serviceConfigure("http://127.0.0.1:9").ok && fx.ws->service().ready(), "le service regle (rien n'y repond)");
        h.openDialog(gui::makeLicenceWizard(h, {reqC}, {}, "Client d'essai Beta"));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Poste essai C") && rows(w, "wizard.demandes") == 1, "la demande donnee a l'ouverture");
        check(next(w), "etape 2");
        check(find<ui::InputText>(w, "wizard.client")->text() == "Client d'essai Beta", "le client prerempli");
        idBeta = find<ui::InputText>(w, "wizard.identifiant")->text();
        check(idBeta != idAlpha && fx.ws->licence(idBeta) == nullptr, "un identifiant neuf");
        check(type(w, "wizard.identifiant", idAlpha), "l'identifiant d'une licence existante");
        t = paintDialog(d);
        check(!next(w) && has(t, "existe d\xC3\xA9j\xC3\xA0"), "une licence existante : refusee");
        check(type(w, "wizard.identifiant", idBeta) && next(w), "etape 3");
        auto* eds = find<ui::TableView>(w, "wizard.editions");
        eds->selectModelRows({static_cast<ui::RowIndex>(rowOf(*eds, 0, "ENTERPRISE"))});
        check(click(w, "wizard.duree.sansfin") && find<ui::RadioButton>(w, "wizard.type.perpetuelle")->selected(), "Sans fin : perpetuelle");
        check(!find<ui::InputText>(w, "wizard.duree")->enabled() && !find<ui::InputText>(w, "wizard.grace")->enabled(), "duree et grace sans objet");
        check(next(w), "etape 4");
        auto* multi = find<ui::Checkbox>(w, "wizard.f.multi_user");
        check(multi && multi->isChecked(), "multi_user : dans ENTERPRISE");
        multi->setState(ui::Checkbox::State::Unchecked);
        t = paintDialog(d);
        check(has(t, "personnalis\xC3\xA9" "es : 0 de plus, 1 de moins"), "personnalisees : le compte le dit");
        check(find<ui::Button>(w, "wizard.fonctionnalites.edition")->enabled(), "Revenir a l'edition : possible");
        check(next(w), "etape 5");
        t = paintDialog(d);
        check(has(t, "perp\xC3\xA9tuelle") && has(t, "sans fin") && has(t, "(personnalis\xC3\xA9" "es)"), "recapitulatif : perpetuelle, personnalisees");
        check(next(w), "etape 6");
        const std::string out = U(fx.root / "envoi" / (idBeta + ".xpglic"));
        rig.picks = {out};
        rig.slowOnThread = true;
        const std::size_t before = rig.reports.size();
        check(click(w, "wizard.signer"), "Signer");
        const gui::LicenceEntry* l = fx.ws->licence(idBeta);
        check(l && l->licence.type == lic::LicenseType::Perpetual && l->licence.expiresAt == 0, "perpetuelle, sans fin");
        check(l && l->licence.grant("multi_user") == nullptr && l->licence.grant("floating_licenses") != nullptr, "multi_user retiree, le reste garde");
        check(rig.reports.size() == before + 1 && rig.reports.back().ok, "la signature est rapportee");
        check(static_cast<bool>(rig.pendingDone) && !fx.ws->hasServiceJob(), "la publication sur le service part d'elle-meme (sur son fil)");
        t = paintDialog(d);
        check(has(t, "publication sur le service est en cours"), "l'assistant le dit pendant ce temps");
        check(rig.finishSlow(), "la publication finit (sur son fil)");
        rig.slowOnThread = false;
        check(rig.reports.size() == before + 2 && !rig.reports.back().ok && has(rig.reports.back().message, "pas de r\xC3\xA9ponse du service"),
              "la publication est rapportee : refusee, rien d'invente (" + (rig.last() ? rig.last()->text() : std::string()) + ")");
        t = paintDialog(d);
        check(has(t, "PAS publi\xC3\xA9" "e sur le service") && has(t, "envoie le fichier au client"), "le refus est montre dans l'assistant");
        check(exists(out), "le fichier est la (a envoyer au client)");
        check(click(w, "wizard.fermer") && closed(d), "Fermer");
        // La suite se passe sans service (le fichier service.json retire).
        std::error_code ec;
        fs::remove(fx.admin / "service.json", ec);
        fx.ws->reload();
        check(!fx.ws->service().configured && !fx.ws->hasServiceJob(), "le service retire pour la suite");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- ajouter un PC a une licence (etendre)\n");
    const std::string reqD = fx.request("pcD", "Poste essai D");
    g_now += 3600;   // une heure plus tard : la nouvelle emission remplace l'ancienne dans l'archive
    {
        h.openDialog(gui::makeLicenceWizard(h, {}, idAlpha));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Ajouter des PC \xC3\xA0 la licence " + idAlpha), "le titre dit la licence etendue");
        check(rows(w, "wizard.pc.existants") == 2 && has(t, "Poste essai A") && has(t, "Poste essai B"), "les PC deja lies sont montres");
        rig.picks = {reqD};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter... (PC D)");
        rig.picks = {reqA};
        check(click(w, "wizard.demandes.ajouter"), "Ajouter... (PC A, deja lie)");
        t = paintDialog(d);
        check(has(t, "d\xC3\xA9j\xC3\xA0 sur la licence"), "un PC deja lie : reactivation");
        check(next(w), "etape 2");
        check(find<ui::InputText>(w, "wizard.identifiant")->text() == idAlpha, "l'identifiant de la licence");
        check(find<ui::InputText>(w, "wizard.client")->text() == "Client d'essai Alpha", "le client de la licence");
        check(!shown(w, "wizard.identifiant.proposer"), "pas de nouvel identifiant a proposer");
        check(next(w), "etape 3");
        check(find<ui::InputText>(w, "wizard.pc")->text() == "3", "3 PC (les 2 lies + 1)");
        check(type(w, "wizard.pc", "2"), "2 PC");
        t = paintDialog(d);
        check(!next(w) && has(t, "Il faut au moins 3 PC : 2 d\xC3\xA9j\xC3\xA0 li\xC3\xA9s"), "moins que les PC lies et demandes : refuse");
        check(type(w, "wizard.pc", "3") && next(w) && next(w) && next(w), "etapes 4, 5, 6");
        rig.picks = {U(fx.root / "envoi" / (idAlpha + "-v2.xpglic"))};
        check(click(w, "wizard.signer"), "Signer");
        const gui::LicenceEntry* l = fx.ws->licence(idAlpha);
        check(l && l->licence.devices.size() == 3 && fx.ws->licences().size() == 2, "la licence a 3 PC (la meme licence, re-emise)");
        check(click(w, "wizard.fermer"), "Fermer");

        h.openDialog(gui::makeLicenceWizard(h, {}, "XPG-2026-99999"));
        check(has(paintDialog(*rig.dialogs.back()), "n'est pas dans l'archive"), "etendre une licence inconnue : refuse");
        check(click(*rig.dialogs.back()->widgetRoot(), "wizard.fermer"), "Fermer");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une demande qui nomme une licence de l'archive\n");
    {
        const std::string reqE = fx.request("pcE", "Poste essai E", idAlpha);
        h.openDialog(gui::makeLicenceWizard(h, {reqE}));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        check(next(w), "etape 2");
        std::string t = paintDialog(d);
        check(shown(w, "wizard.etendre") && has(t, "demandent la licence " + idAlpha), "l'assistant propose d'y ajouter le PC");
        check(!next(w), "un autre identifiant que celui demande : refuse");
        check(click(w, "wizard.etendre"), "Ajouter ces PC a la licence");
        check(find<ui::InputText>(w, "wizard.identifiant")->text() == idAlpha && next(w), "etendue : son identifiant, Suivant possible");
        check(click(w, "wizard.annuler") && closed(d), "Annuler");
        check(fx.ws->licence(idAlpha)->licence.devices.size() == 3, "annule : rien n'est signe");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la page Licences : liste, filtres, recherche, detail\n");
    // Une licence d'evaluation de 30 jours, puis 31 jours plus tard : expiree.
    const std::string idGamma = "XPG-2026-00090";
    check(fx.licence(idGamma, "Client d'essai Gamma", "EVALUATION", {fx.request("pcF", "Poste essai F")}, 30).ok, "licence d'evaluation signee");
    g_now += 31 * kDay;
    fx.ws->reload();
    auto page = gui::makeLicencesPage(h);
    rig.refreshers.push_back([&] { page->refresh(); });
    {
        std::string t = paintPage(*page);
        check(page->title() == "Licences" && has(t, "Licences"), "le titre");
        check(has(t, idAlpha) && has(t, "Client d'essai Alpha") && has(t, "3 / 3") && has(t, "PROFESSIONAL"), "Alpha : id, client, PC, edition");
        // 1.41.0 : le type et la fin sont dans la fiche (la liste les garde dans son modele, sans les montrer a cette largeur).
        {
            auto* tv = find<ui::TableView>(*page, "licences.table");
            const int rb = tv ? rowOf(*tv, 0, idBeta) : -1;
            checkText(has(t, idBeta) && rb >= 0 && cell(*page, "licences.table", rb, 3) == "perp\xC3\xA9tuelle" && cell(*page, "licences.table", rb, 6) == "sans fin",
                      "Beta : perpetuelle, sans fin", t);
        }
        check(has(t, "Client d'essai Gamma") && has(t, "expir\xC3\xA9" "e"), "Gamma : expiree");
        // 1.47.0 : l'etat est une puce de la barre << Filtrer et chercher >> (le champ 8 : Etat) ; son menu compte chaque valeur.
        auto* bar = find<ui::FilterBar>(*page, "licences.barre");
        const auto stateCount = [&](const std::string& v) {
            if (!bar) return std::size_t{999};
            for (const auto& [k, n] : bar->valueCounts(8))
                if (k == v) return n;
            return std::size_t{0};
        };
        check(bar && stateCount("Active") == 2 && stateCount("Expir\xC3\xA9" "e") == 1 && stateCount("R\xC3\xA9voqu\xC3\xA9" "e") == 0 && has(t, "\xC3\x89tat") &&
                  has(t, "Offre") && bar->countText() == "3 sur 3",
              "la barre : l'etat et ses comptes, l'offre, 3 sur 3");
        check(rows(*page, "licences.table") == 3, "3 licences");
        const auto pickState = [&](const std::string& v) {
            if (!bar) return false;
            if (v.empty()) bar->clearField(8);
            else bar->setValues(8, {v});
            return true;
        };
        check(pickState("Expir\xC3\xA9" "e") && rows(*page, "licences.table") == 1, "filtre Expirees");
        check(pickState("Active") && rows(*page, "licences.table") == 2, "filtre Actives");
        check(pickState("") && rows(*page, "licences.table") == 3, "filtre Toutes");
        check(type(*page, "licences.recherche", "beta") && rows(*page, "licences.table") == 1, "recherche : client");
        check(type(*page, "licences.recherche", "evaluation") && rows(*page, "licences.table") == 1, "recherche : edition");
        check(type(*page, "licences.recherche", "zzz") && rows(*page, "licences.table") == 0, "recherche : rien");
        t = paintPage(*page);
        check(has(t, "Aucune licence ne correspond"), "rien ne correspond : la page le dit");
        check(type(*page, "licences.recherche", ""), "recherche videe");
        check(!click(*page, "licences.exporter") && !click(*page, "licences.revoquer") && !click(*page, "licences.ajouter-pc"), "sans licence choisie : grises");
        check(has(t, "Choisis une licence"), "le detail invite a choisir");

        auto* table = find<ui::TableView>(*page, "licences.table");
        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idAlpha))});
        t = paintPage(*page, 1400.f, 900.f);
        auto* grid = find<ui::PropertyGrid>(*page, "licences.details");
        bool client = false, file = false;
        for (const auto& c : grid->categories())
            for (const auto& p : c.properties) {
                if (p.name == "Client" && p.value == "Client d'essai Alpha") client = true;
                if (p.name == "Fichier" && has(p.value, idAlpha)) file = true;
            }
        check(client && file, "le detail : client, fichier de l'archive");
        check(rows(*page, "licences.pcs") == 3 && has(t, "Poste essai D"), "le detail : ses 3 PC");
        bool features = false;
        for (const auto& c : grid->categories()) features = features || c.name == "Fonctionnalit\xC3\xA9s (18)";
        check(features, "le detail : ses fonctionnalites");
    }

    std::printf("-- la page Licences : exporter, ajouter un PC, nouvelle licence, dossier, glisser\n");
    {
        const std::string out = U(fx.root / "envoi" / "export-alpha");
        rig.picks = {out};
        check(click(*page, "licences.exporter"), "Exporter le fichier...");
        check(exists(out + ".xpglic") && rig.last() && rig.last()->ok, "le fichier est ecrit, le resultat rapporte");
        rig.picks = {""};
        check(click(*page, "licences.exporter") && lastFlash(rig, "Annul"), "exporter annule : dit");
        startBusy(rig);
        const std::size_t asked = rig.asked.size();
        check(click(*page, "licences.exporter") && lastFlash(rig, "travail est en cours") && rig.asked.size() == asked, "occupe : exporter refuse");
        endBusy(rig);

        const std::size_t n = rig.dialogs.size();
        check(click(*page, "licences.ajouter-pc") && rig.dialogs.size() == n + 1, "Ajouter un PC... ouvre l'assistant");
        check(has(paintDialog(*rig.dialogs.back()), "Ajouter des PC \xC3\xA0 la licence " + idAlpha), "sur la licence choisie");
        check(click(*rig.dialogs.back()->widgetRoot(), "wizard.annuler"), "Annuler");
        check(click(*page, "licences.nouvelle") && has(paintDialog(*rig.dialogs.back()), "Nouvelle licence"), "Nouvelle licence... ouvre l'assistant");
        check(click(*rig.dialogs.back()->widgetRoot(), "wizard.annuler"), "Annuler");
        check(click(*page, "licences.dossier") && has(rig.openedFolders.back(), "licences"), "Ouvrir le dossier de l'archive");

        const std::string reqG = fx.request("pcG", "Poste essai G"), reqH = fx.request("pcH", "Poste essai H");
        const std::size_t m = rig.dialogs.size();
        check(page->dropFiles({reqG}) && rig.dialogs.size() == m + 1, "glisser une demande : l'assistant s'ouvre");
        ui::Widget& w = *rig.dialogs.back()->widgetRoot();
        check(rows(w, "wizard.demandes") == 1, "avec elle");
        check(page->dropFiles({reqH}) && rig.dialogs.size() == m + 1 && rows(w, "wizard.demandes") == 2, "glisser encore : l'assistant ouvert la prend");
        check(!page->dropFiles({fileAlpha}), "un .xpglic : pas pour la page (la coquille le range)");
        check(!page->dropFiles({reqG, fileAlpha}), "un melange : pour la coquille");
        const std::string reqI = fx.request("pcI", "Poste essai I");
        check(rig.dialogs.back()->HandleEvent(ui::InputEvent{ui::FileDropped{reqI, {600.f, 400.f}}}) == ui::EventResult::Consumed && rows(w, "wizard.demandes") == 3,
              "un .xpgreq lache sur l'assistant ouvert (au-dessus de la coquille) : ajoute");
        check(rig.dialogs.back()->HandleEvent(ui::InputEvent{ui::FileDropped{fileAlpha, {600.f, 400.f}}}) == ui::EventResult::Consumed && rows(w, "wizard.demandes") == 3
                  && has(paintDialog(*rig.dialogs.back()), "n'est pas une demande"),
              "un .xpglic lache sur l'assistant : refuse, dit");
        check(click(w, "wizard.annuler"), "Annuler");
        check(page->dropFiles({reqG}) && rig.dialogs.size() == m + 2, "l'assistant ferme ne prend plus rien : un nouveau s'ouvre");
        check(click(*rig.dialogs.back()->widgetRoot(), "wizard.annuler"), "Annuler");
    }

    std::printf("-- revoquer depuis la page Licences\n");
    {
        auto* table = find<ui::TableView>(*page, "licences.table");
        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idBeta))});
        check(click(*page, "licences.revoquer"), "Revoquer...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "R\xC3\xA9voquer") && has(t, idBeta) && has(t, "Client d'essai Beta"), "le dialogue dit la licence");
        check(has(t, "service en ligne"), "le dialogue dit comment la revocation arrive");
        check(click(w, "revoquer.ok"), "Revoquer sans raison");
        t = paintDialog(d);
        check(!fx.ws->hasRevocations() && has(t, "Donne une raison") && !closed(d), "sans raison : refuse");
        startBusy(rig);
        check(type(w, "revoquer.raison", "Fin de contrat (essai)") && click(w, "revoquer.ok"), "Revoquer (occupe)");
        check(!fx.ws->hasRevocations() && has(paintDialog(d), "travail est en cours"), "occupe : refuse");
        endBusy(rig);
        check(click(w, "revoquer.ok"), "Revoquer");
        check(fx.ws->hasRevocations() && fx.ws->revocations().revoked.size() == 1 && fx.ws->revocations().revoked[0].device.empty(), "toute la licence revoquee");
        check(closed(d) && rig.last() && rig.last()->ok, "le dialogue se ferme, le resultat est rapporte");
        t = paintPage(*page);
        {
            std::size_t revoked = 0;
            if (auto* b = find<ui::FilterBar>(*page, "licences.barre"))
                for (const auto& [k, n] : b->valueCounts(8))
                    if (k == "R\xC3\xA9voqu\xC3\xA9" "e") revoked = n;
            check(revoked == 1 && fx.ws->licence(idBeta)->revoked, "la page la montre revoquee (la puce Etat : 1 revoquee)");
        }

        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idAlpha))});
        check(click(*page, "licences.revoquer"), "Revoquer... (un PC)");
        auto& d2 = *rig.dialogs.back();
        ui::Widget& w2 = *d2.widgetRoot();
        (void)paintDialog(d2);
        find<ui::RadioButton>(w2, "revoquer.unpc")->select();
        check(type(w2, "revoquer.raison", "PC remplac\xC3\xA9 (essai)") && click(w2, "revoquer.ok"), "Revoquer un PC sans le choisir");
        check(fx.ws->revocations().revoked.size() == 1 && has(paintDialog(d2), "Choisis le PC"), "le PC a choisir");
        auto* pcs = find<ui::DropDown>(w2, "revoquer.pc");
        check(pcs && pcs->items().size() == 3, "les 3 PC de la licence");
        const std::string key = fx.ws->licence(idAlpha)->licence.devices[0].key;
        int pick = -1;
        for (std::size_t i = 0; i < pcs->items().size(); ++i)
            if (pcs->items()[i].value == key) pick = static_cast<int>(i);
        pcs->setSelectedIndex(pick);
        check(click(w2, "revoquer.ok"), "Revoquer le PC");
        check(fx.ws->revocations().revoked.size() == 2 && fx.ws->revocations().revoked[1].device == key, "un PC revoque");
        check(closed(d2) && fx.ws->licence(idAlpha)->revokedDevices == 1, "la licence a 1 PC revoque");
        t = paintPage(*page, 1400.f, 900.f);
        checkText(has(t, "r\xC3\xA9voqu\xC3\xA9 le " + gui::dateFr(g_now)), "le detail marque le PC revoque", t);
        bool reason = false;
        for (const auto& c : find<ui::PropertyGrid>(*page, "licences.details")->categories())
            for (const auto& p : c.properties) reason = reason || has(p.value, "PC remplac\xC3\xA9 (essai)");
        check(reason, "le detail dit la raison de la revocation");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la page Revocations\n");
    {
        auto rp = gui::makeRevocationsPage(h);
        rig.refreshers.push_back([&] { rp->refresh(); });
        std::string t = paintPage(*rp);
        check(rp->title() == "R\xC3\xA9vocations", "le titre");
        check(has(t, "Liste n\xC2\xB0 ") && has(t, "2 r\xC3\xA9vocations"), "le numero de la liste, le compte");
        check(has(t, "service en ligne") && has(t, "importe"), "la page dit comment une revocation arrive");
        check(has(t, "toute la licence") && has(t, "Fin de contrat (essai)") && has(t, "Client d'essai Beta"), "la licence revoquee");
        check(has(t, "Poste essai A") && has(t, "PC remplac\xC3\xA9 (essai)") && has(t, "toutes"), "le PC revoque, par son nom");
        check(!click(*rp, "revocations.retirer"), "sans choix : Retirer grise");

        const std::string out = U(fx.root / "envoi" / "liste");
        rig.picks = {out};
        check(click(*rp, "revocations.exporter") && exists(out + ".xpgrev") && rig.last()->ok, "Exporter la liste...");
        check(rig.asked.back().save && has(rig.asked.back().start, ".xpgrev"), "un .xpgrev propose");

        check(click(*rp, "revocations.revoquer"), "Revoquer...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        auto* which = find<ui::DropDown>(w, "revoquer.licence");
        check(which && which->items().size() == 3, "la licence se choisit dans une liste");
        check(click(w, "revoquer.ok") && has(paintDialog(d), "Choisis la licence"), "sans licence choisie : refuse");
        for (std::size_t i = 0; i < which->items().size(); ++i)
            if (which->items()[i].value == idGamma) which->setSelectedIndex(static_cast<int>(i));
        find<ui::Checkbox>(w, "revoquer.emissions")->setState(ui::Checkbox::State::Checked);
        check(type(w, "revoquer.raison", "Essai termin\xC3\xA9") && click(w, "revoquer.ok"), "Revoquer Gamma (les emissions signees)");
        check(fx.ws->revocations().revoked.size() == 3 && fx.ws->revocations().revoked[2].issuedBefore != 0, "revoquee : les emissions d'avant");
        t = paintPage(*rp);
        check(has(t, "avant le ") && has(t, "3 r\xC3\xA9vocations"), "la page la montre");

        auto* table = find<ui::TableView>(*rp, "revocations.table");
        table->selectModelRows({0});
        check(click(*rp, "revocations.retirer"), "Retirer la revocation");
        auto& c = *rig.dialogs.back();
        t = paintDialog(c);
        check(has(t, "Retirer la r\xC3\xA9vocation") && has(t, idBeta), "la confirmation dit laquelle");
        check(click(*c.widgetRoot(), "confirmer.annuler") && closed(c) && fx.ws->revocations().revoked.size() == 3, "Annuler : rien ne change");
        startBusy(rig);
        check(click(*rp, "revocations.retirer") && lastFlash(rig, "travail est en cours"), "occupe : refuse");
        endBusy(rig);
        check(click(*rp, "revocations.retirer"), "Retirer la revocation (encore)");
        check(click(*rig.dialogs.back()->widgetRoot(), "confirmer.ok"), "Confirmer");
        check(fx.ws->revocations().revoked.size() == 2 && !fx.ws->licence(idBeta)->revoked, "la revocation de Beta est retiree");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "retir"), "le resultat est rapporte");
        t = paintPage(*rp);
        check(!has(t, "Fin de contrat (essai)") && has(t, "2 r\xC3\xA9vocations"), "la page suit");
    }

    // ------------------------------------------------------------------------------------------------ 1.35.0
    std::printf("-- une licence en ligne : l'assistant (sans service : grise ; le service ne repond pas : aucun code montre)\n");
    const std::string testUrl = "http://127.0.0.1:9";   // un service d'essai sur ce poste ou rien n'ecoute
    std::string idDelta;
    rig.refreshers.clear();   // la page Revocations d'avant est fermee
    {
        auto lp = gui::makeLicencesPage(h);
        (void)paintPage(*lp);
        check(shown(*lp, "licences.nouvelle-en-ligne") && !find<ui::Button>(*lp, "licences.nouvelle-en-ligne")->enabled() && !shown(*lp, "licences.relever"),
              "sans service : Nouvelle licence en ligne grisee, pas de Relever");
        h.openDialog(gui::makeLicenceWizard(h, {}, {}, "Client d'essai Delta"));
        auto& d0 = *rig.dialogs.back();
        (void)paintDialog(d0);
        ui::Widget& w0 = *d0.widgetRoot();
        check(shown(w0, "wizard.mode.en-ligne") && !find<ui::RadioButton>(w0, "wizard.mode.en-ligne")->enabled() &&
                  find<ui::RadioButton>(w0, "wizard.mode.lies")->selected(),
              "sans service : << En ligne >> grise, << Lier des PC >> choisi");
        check(click(w0, "wizard.annuler") && closed(d0), "Annuler");

        check(fx.ws->serviceConfigure(testUrl).ok && fx.ws->service().ready(), "le service regle (rien n'y repond)");
        lp->refresh();
        (void)paintPage(*lp);
        check(find<ui::Button>(*lp, "licences.nouvelle-en-ligne")->enabled() && shown(*lp, "licences.relever"), "le service regle : Nouvelle licence en ligne, Relever");
        const std::size_t n = rig.dialogs.size();
        check(click(*lp, "licences.nouvelle-en-ligne") && rig.dialogs.size() == n + 1, "Nouvelle licence en ligne... ouvre l'assistant");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(find<ui::RadioButton>(w, "wizard.mode.en-ligne")->selected() && shown(w, "wizard.bail") && !shown(w, "wizard.demandes.ajouter"),
              "en ligne : le bail, aucune demande a choisir");
        check(find<ui::InputText>(w, "wizard.bail")->text() == "30", "le bail : 30 jours par defaut");
        checkText(has(t, "CODE D'ACTIVATION") && has(t, "Nouveau code"), "l'assistant dit le code, et quoi faire s'il est perdu", t);
        check(type(w, "wizard.bail", "0") && !next(w), "un bail de 0 jour : refuse");
        check(type(w, "wizard.bail", "45") && next(w), "45 jours : etape 2");
        check(find<ui::InputText>(w, "wizard.client")->text().empty() && type(w, "wizard.client", "Client d'essai Delta"), "le client");
        idDelta = find<ui::InputText>(w, "wizard.identifiant")->text();
        check(!idDelta.empty() && fx.ws->licence(idDelta) == nullptr && next(w), "un identifiant neuf : etape 3");
        auto* eds = find<ui::TableView>(w, "wizard.editions");
        eds->selectModelRows({static_cast<ui::RowIndex>(rowOf(*eds, 0, "ENTERPRISE"))});
        auto* floating = find<ui::RadioButton>(w, "wizard.type.flottante");
        check(floating && floating->enabled(), "en ligne : flottante possible");
        if (floating) floating->select();
        check(type(w, "wizard.pc", "2") && next(w), "2 postes en meme temps : etape 4");
        check(next(w), "etape 5");
        t = paintDialog(d);
        checkText(has(t, "flottante") && has(t, "2 postes en m\xC3\xAAme temps, bail 45 j, un code neuf"), "recapitulatif : flottante, en ligne, le bail", t);
        check(next(w) && find<ui::Button>(w, "wizard.signer")->text() == "Signer et d\xC3\xA9poser", "etape 6 : Signer et deposer");
        rig.slowOnThread = true;
        const std::size_t before = rig.reports.size(), dialogs = rig.dialogs.size();
        check(click(w, "wizard.signer"), "Signer et deposer");
        const gui::LicenceEntry* l = fx.ws->licence(idDelta);
        check(l && l->licence.service == testUrl && l->licence.devices.empty() && l->licence.type == lic::LicenseType::Floating && l->licence.maxDevices == 2,
              "signee et rangee : en ligne, sans PC lie, flottante, 2 postes");
        t = paintDialog(d);
        check(has(t, "d\xC3\xA9p\xC3\xB4t sur le service est en cours"), "le depot est en cours (sur son fil)");
        check(rig.finishSlow(), "le depot finit");
        rig.slowOnThread = false;
        check(rig.reports.size() == before + 2 && !rig.reports.back().ok && has(rig.reports.back().message, "pas de r\xC3\xA9ponse du service"),
              "le depot est rapporte : refuse (" + (rig.last() ? rig.last()->text() : std::string()) + ")");
        check(rig.dialogs.size() == dialogs, "le depot refuse : aucun code montre");
        t = paintDialog(d);
        check(has(t, "PAS d\xC3\xA9pos\xC3\xA9" "e sur le service"), "l'assistant le dit");
        check(!shown(w, "wizard.dossier") && shown(w, "wizard.fermer"), "pas de fichier a envoyer : pas de dossier");
        check(!codeWritten(fx.root), "le code n'est ecrit dans aucun fichier");
        check(click(w, "wizard.fermer") && closed(d), "Fermer");
    }

    std::printf("-- la page Licences : une licence en ligne (ses activations, Retirer ce PC, Nouveau code, Desactiver, Relever : hors ligne, dit)\n");
    {
        // Un releve du service, ecrit comme l'outil l'ecrit : un PC active, un retire.
        online::Snapshot snap;
        snap.at = g_now - 600;
        snap.url = testUrl;
        online::OnlineLicence ol;
        ol.licenseId = idDelta;
        ol.issuedAt = fx.ws->licence(idDelta) ? fx.ws->licence(idDelta)->licence.issuedAt : 0;
        ol.type = "floating";
        ol.seats = 2;
        ol.leaseDays = 45;
        ol.hasCode = true;
        online::OnlineActivation a1;
        a1.deviceKey = std::string(64, 'a');
        a1.name = "Poste essai en ligne 1";
        a1.state = "active";
        a1.activatedAt = g_now - 3 * kDay;
        a1.seenAt = g_now - 600;
        a1.until = g_now + 44 * kDay;
        online::OnlineActivation a2 = a1;
        a2.deviceKey = std::string(64, 'b');
        a2.name = "Poste essai en ligne 2";
        a2.state = "retiree";
        ol.activations = {a1, a2};
        snap.licences = {ol};
        std::string why;
        check(online::writeSnapshot(U(fx.admin), snap, &why), "un releve d'essai (" + why + ")");
        fx.ws->reload();
        auto lp = gui::makeLicencesPage(h);
        auto* table = find<ui::TableView>(*lp, "licences.table");
        table->selectModelRows({static_cast<ui::RowIndex>(rowOf(*table, 0, idDelta))});
        std::string t = paintPage(*lp, 1400.f, 900.f);
        const int row = rowOf(*table, 0, idDelta);
        check(cell(*lp, "licences.table", row, 3) == "flottante (en ligne)" && cell(*lp, "licences.table", row, 4) == "1 / 2 en ligne", "la liste : en ligne, ses PC actives");
        check(shown(*lp, "licences.nouveau-code") && shown(*lp, "licences.desactiver") && find<ui::Button>(*lp, "licences.ajouter-pc")->text() == "Prolonger\xE2\x80\xA6" &&
                  find<ui::Button>(*lp, "licences.retirer-pc")->text() == "Retirer ce PC\xE2\x80\xA6",
              "une licence en ligne : Prolonger, Retirer ce PC, Nouveau code, Desactiver");
        // 1.41.0 : la fiche montre trois boutons ; le reste est dans le menu << ... >> (le dangereux a la fin).
        {
            check(click(*lp, "licences.plus"), "le menu ...");
            auto* menu = find<ui::PopupMenu>(*lp, "licences.menu");
            std::vector<std::string> labels;
            if (menu)
                for (const auto& it : menu->items())
                    if (!it.separator) labels.push_back(it.label);
            const auto in = [&](const std::string& l) { return std::find(labels.begin(), labels.end(), l) != labels.end(); };
            check(menu && menu->isOpen() && in("Retirer ce PC\xE2\x80\xA6") && in("Nouveau code\xE2\x80\xA6") && in("D\xC3\xA9sactiver\xE2\x80\xA6") &&
                      in("R\xC3\xA9voquer\xE2\x80\xA6") && in("Ouvrir le dossier de l'archive") && labels.back() == "Supprimer\xE2\x80\xA6",
                  "le menu ... : retirer, nouveau code, desactiver, revoquer, supprimer (1.48.1 : toujours la, a la fin), le dossier");
            if (menu) menu->close();
        }
        check(rows(*lp, "licences.pcs") == 2 && cell(*lp, "licences.pcs", 0, 0) == "Poste essai en ligne 1" && cell(*lp, "licences.pcs", 1, 1) == "retir\xC3\xA9" "e" &&
                  cell(*lp, "licences.pcs", 0, 5) == gui::dateFr(g_now + 44 * kDay) && has(t, "Vu le"),
              "ses activations, au dernier releve (le bail de chacune)");
        bool where = false, onService = false;
        for (const auto& c : find<ui::PropertyGrid>(*lp, "licences.details")->categories())
            for (const auto& p : c.properties) {
                where = where || (p.name == "En ligne" && p.value == testUrl);
                onService = onService || (p.name == "Sur le service" && has(p.value, "1 PC activ\xC3\xA9(s), bail de 45 j"));
            }
        check(where && onService, "le detail : le service, ce qu'il en dit");

        check(click(*lp, "licences.retirer-pc") && lastFlash(rig, "Choisis d'abord le PC"), "Retirer ce PC sans choix : dit");
        find<ui::TableView>(*lp, "licences.pcs")->selectModelRows({0});
        std::size_t n = rig.dialogs.size();
        check(click(*lp, "licences.retirer-pc") && rig.dialogs.size() == n + 1, "Retirer ce PC... : une confirmation");
        t = paintDialog(*rig.dialogs.back());
        check(has(t, "Retirer ce PC") && has(t, idDelta), "elle dit de quelle licence");
        std::size_t before = rig.reports.size();
        check(click(*rig.dialogs.back()->widgetRoot(), "confirmer.ok"), "Retirer");
        check(rig.reports.size() == before + 1 && !rig.reports.back().ok && has(rig.reports.back().message, "pas de r\xC3\xA9ponse du service"),
              "hors ligne : le refus est dit (" + (rig.last() ? rig.last()->text() : std::string()) + ")");
        const auto* still = fx.ws->service().snapshot.licence(idDelta);
        check(still && still->activations.size() == 2 && still->activations[0].state == "active", "rien d'invente : le releve reste");

        n = rig.dialogs.size();
        check(click(*lp, "licences.nouveau-code") && rig.dialogs.size() == n + 1, "Nouveau code... : une confirmation");
        t = paintDialog(*rig.dialogs.back());
        check(has(t, "nouveau code") && has(t, idDelta), "elle dit ce qui arrive a l'ancien");
        before = rig.reports.size();
        check(click(*rig.dialogs.back()->widgetRoot(), "confirmer.ok"), "Tirer un nouveau code");
        check(rig.reports.size() == before + 1 && !rig.reports.back().ok && has(rig.reports.back().message, "pas de r\xC3\xA9ponse du service"), "hors ligne : refuse, dit");
        check(rig.dialogs.size() == n + 1, "aucun code montre");
        check(!codeWritten(fx.root), "le code tire n'est ecrit nulle part");

        n = rig.dialogs.size();
        check(click(*lp, "licences.desactiver") && rig.dialogs.size() == n + 1, "Desactiver... : une confirmation");
        t = paintDialog(*rig.dialogs.back());
        check(has(t, "D\xC3\xA9sactiver la licence en ligne " + idDelta), "elle dit laquelle");
        before = rig.reports.size();
        check(click(*rig.dialogs.back()->widgetRoot(), "confirmer.ok") && rig.reports.size() == before + 1 && !rig.reports.back().ok, "hors ligne : refuse, dit");
        check(fx.ws->service().snapshot.licence(idDelta) && fx.ws->service().snapshot.licence(idDelta)->active, "toujours active au releve");

        before = rig.reports.size();
        check(click(*lp, "licences.relever") && rig.reports.size() == before + 1 && !rig.reports.back().ok, "Relever hors ligne : refuse, dit");
        check(fx.ws->service().snapshot.licence(idDelta) != nullptr, "le dernier releve reste");
    }

    std::printf("-- le code d'activation : masque, Afficher, Copier, oublie a la fermeture\n");
    {
        const std::string code = "XPG-ESSA-IAAA-BBBB-CCCC";   // un faux code (aucun service ne le connait)
        h.openDialog(gui::makeCodeDialog(idDelta, code));
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Code d'activation") && has(t, idDelta), "le titre dit la licence");
        check(!has(t, code) && has(t, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-"), "masque d'abord");
        check(click(w, "code.afficher") && has(paintDialog(d), code), "Afficher : le code");
        check(find<ui::Button>(w, "code.afficher")->text() == "Masquer" && click(w, "code.afficher") && !has(paintDialog(d), code), "Masquer");
        g_copied.clear();
        check(click(w, "code.copier") && g_copied == code, "Copier : le code dans le presse-papiers, sur ce clic");
        check(has(paintDialog(d), "Copi\xC3\xA9"), "Copie : dit");
        g_copied.clear();
        check(click(w, "code.fermer") && closed(d), "J'ai note le code : ferme");
        check(!codeWritten(fx.root), "le code n'est ecrit nulle part");
    }

    return finish();
}
