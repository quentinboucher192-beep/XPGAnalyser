// =============================================================================
//  tests/admin_gui_demandes_test.cpp - 1.34.0 : LA PAGE << Demandes en ligne >>
//  ET LA CARTE << Passerelle des clients >> de l'application de l'editeur,
//  dessinees a blanc, contre le faux GitHub (aucun reseau)
// -----------------------------------------------------------------------------
//  La passerelle pas reglee (la page dit quoi faire), la carte de la page
//  GitHub (le depot, le jeton des demandes masque, Verifier, Publier la
//  configuration), Relever (la table : client, PC, logiciel, licence demandee,
//  etat), le tableau de bord (le compte et sa date), Accepter... (l'assistant
//  avec la demande ; << Signer et publier >> sans rien demander ; la licence
//  publiee pour le PC, l'issue fermee), une licence partagee (l'assistant en
//  ajout de PC), Refuser... (la raison demandee), Voir sur GitHub, un travail
//  en cours (refuse), une revocation publiee (publishToGateway), le kit des
//  Studios refuse avec une cle d'essai.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "fake_passerelle.hpp"

#include "../src/license_admin/GitHubSync.hpp"
#include "../src/license_admin/Passerelle.hpp"
#include "../src/licensing/Gateway.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace admintest;
namespace gw = licensing::gateway;
namespace pas = license_admin::passerelle;
namespace json = core::json;

namespace {

std::string paintPage(ui::Widget& w, float width = 1280.f, float height = 760.f) {
    TextRenderer r;
    return paint(w, r, width, height);
}

std::string paintDialog(menu::IMenu& m) {
    TextRenderer r;
    return paintMenu(m, r, 1280.f, 800.f);
}

bool shown(ui::Widget& root, std::string_view id) {
    ui::Widget* w = root.findById(id);
    return w && w->visible();
}

int rowOf(ui::TableView& t, std::size_t col, const std::string& text) {
    const auto& m = t.model();
    if (!m) return -1;
    for (std::size_t r = 0; r < m->rowCount(); ++r)
        if (m->cellText(static_cast<ui::RowIndex>(r), col).find(text) != std::string::npos) return static_cast<int>(r);
    return -1;
}

std::size_t rows(ui::Widget& root, std::string_view id) {
    auto* t = find<ui::TableView>(root, id);
    return t && t->model() ? t->model()->rowCount() : 0;
}

bool next(ui::Widget& w) { return click(w, "wizard.suivant"); }

// ---- un PC d'essai, comme un Studio ----
struct Pc {
    std::unique_ptr<lic::LicenseStore> store;
    std::string                        device, nonce;
    int                                issue{0};
};

Pc ask(Fixture& fx, fakegh::FakePasserelle& fake, const lic::SigningKey& editor, const std::string& name, const std::string& customer,
       const std::string& licenseId = {}) {
    Pc pc;
    const fs::path folder = fx.root / ("pc-" + name);
    std::error_code ec;
    fs::create_directories(folder, ec);
    pc.store = std::make_unique<lic::LicenseStore>(fx.pcOptions(folder));
    std::string block, why;
    lic::ActivationRequest parsed;
    if (!pc.store->activationRequestText("Poste essai " + name, licenseId, "API Studio 1.34.0 (essai)", block, &parsed, &why)) {
        std::printf("  (demande : %s)\n", why.c_str());
        return pc;
    }
    deploy::crypto::Bytes raw;
    deploy::crypto::PublicKey pub{};
    (void)deploy::crypto::unhex(parsed.deviceKey, raw, 32);
    std::copy(raw.begin(), raw.end(), pub.begin());
    pc.device = gw::deviceId(pub);
    pc.nonce = parsed.nonce;
    gw::OnlineRequest q;
    q.request = block;
    q.customer = customer;
    q.message = "Message d'essai de " + name;
    json::Value v = json::Value::object();
    v.set("title", gw::issueTitle(pc.device));
    v.set("body", gw::issueBody(pc.device, editor.keyId(), gw::sealFor(editor.publicKey(), gw::writeOnlineRequest(q))));
    app::ai::HttpRequest req;
    req.method = "POST";
    req.url = fake.api + "/repos/" + fake.repo + "/issues";
    req.body = json::write(v, false);
    req.headers = {{"Authorization", "Bearer " + fake.requestToken}};
    std::string got;
    app::ai::HttpSink sink;
    sink.onData = [&](std::string_view d) { got.append(d); };
    (void)app::ai::makeHttpClient()->perform(req, sink);
    json::Value answer;
    if (json::parse(got, answer) && answer.isObject()) pc.issue = static_cast<int>(answer.integer("number"));
    return pc;
}

bool statusOf(fakegh::FakePasserelle& fake, const std::string& device, gw::DeviceStatus& out) {
    const auto it = fake.files.find(gw::statusPath(device));
    if (it == fake.files.end()) return false;
    std::vector<lic::SignedBlock> blocks;
    return lic::readBlocks(it->second, blocks, nullptr) && blocks.size() == 1 && gw::parseDeviceStatus(blocks[0].document, out, nullptr);
}

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

// Choisir la ligne d'une table dont la colonne contient `text`.
bool selectRow(ui::Widget& root, std::size_t col, const std::string& text) {
    auto* t = find<ui::TableView>(root, "demandes.table");
    if (!t) return false;
    const int r = rowOf(*t, col, text);
    if (r < 0) return false;
    t->selectModelRows({static_cast<ui::RowIndex>(r)});
    return true;
}

// L'assistant ouvert sur une demande : jusqu'a << Signer et publier >>.
bool walkToSign(menu::IMenu& d, bool chooseEdition) {
    ui::Widget& w = *d.widgetRoot();
    (void)paintDialog(d);
    if (!next(w)) return false;   // 2. client
    (void)paintDialog(d);
    if (!next(w)) return false;   // 3. edition
    if (chooseEdition) {
        auto* eds = find<ui::TableView>(w, "wizard.editions");
        if (!eds) return false;
        const int pro = rowOf(*eds, 0, "PROFESSIONAL");
        if (pro < 0) return false;
        eds->selectModelRows({static_cast<ui::RowIndex>(pro)});
    }
    (void)paintDialog(d);
    if (!next(w)) return false;   // 4. fonctionnalites
    (void)paintDialog(d);
    if (!next(w)) return false;   // 5. recapitulatif
    (void)paintDialog(d);
    return next(w);               // 6. signer
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage : admin_gui_demandes_test <racine du depot>\n");
        return 2;
    }
    const std::string source = argv[1];
    installMetrics();
    Fixture fx(source, "demandes");
    check(fx.ready(), "le poste d'essai est pret (cle d'essai, registre)");
    std::string keyWhy;
    const auto editorKey = license_admin::loadPublisherKey(U(fx.admin), &keyWhy);
    if (!editorKey) return finish();
    const lic::SigningKey& editor = *editorKey;
    fakegh::FakePasserelle fake;
    fakegh::install(&fake);
    HostRig rig(fx);
    std::vector<std::string> urls;
    auto h = rig.host();
    h.openUrl = [&urls](const std::string& u) { urls.push_back(u); };

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la passerelle pas reglee\n");
    auto page = gui::makeRequestsPage(h);
    {
        const std::string t = paintPage(*page);
        check(has(t, "Demandes en ligne") && page->title() == "Demandes en ligne", "le titre");
        check(has(t, "pas encore r\xC3\xA9gl\xC3\xA9" "e") && has(t, "Passerelle des clients") && has(t, "Noter des demandes"), "la page dit quoi faire (et le chemin par fichier)");
        check(shown(*page, "demandes.regler") && !shown(*page, "demandes.relever") && !shown(*page, "demandes.accepter"), "seulement << Regler la passerelle >>");
        check(click(*page, "demandes.regler") && !rig.navigations.empty() && rig.navigations.back() == "github", "Regler -> Synchronisation GitHub");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la carte << Passerelle des clients >> (Synchronisation GitHub)\n");
    {
        check(fx.ws->githubSaveToken(fake.adminToken).ok, "le jeton GitHub de l'editeur (celui de la synchro)");
        auto gh = gui::makeGithubPage(h);
        std::string t = paintPage(*gh, 1280.f, 4000.f);
        check(has(t, "Passerelle des clients") && has(t, "PUBLIC") && has(t, "Fine-grained tokens") && has(t, "Issues : Read and write") &&
                  has(t, "Contents : Read and write"),
              "la carte : le depot public, le chemin du jeton des demandes, ce que doit couvrir le tien");
        check(!find<ui::Button>(*gh, "github.passerelle.publier")->enabled(), "Publier : grise tant que rien n'est regle");
        check(type(*gh, "github.passerelle.depot", "https://github.com/" + fake.repo + ".git") && click(*gh, "github.passerelle.enregistrer"),
              "le depot (l'adresse du navigateur)");
        check(fx.ws->passerelle().configured && fx.ws->passerelle().config.repo == fake.repo, "reglee : " + (rig.last() ? rig.last()->text() : std::string()));
        auto* field = find<ui::InputText>(*gh, "github.passerelle.jeton");
        check(field && field->masked(), "le champ du jeton des demandes est masque");
        check(type(*gh, "github.passerelle.jeton", fake.adminToken) && click(*gh, "github.passerelle.jeton.enregistrer"), "(ton propre jeton, colle par erreur)");
        check(rig.last() && !rig.last()->ok && has(rig.last()->message, "TON jeton") && field->text().empty(), "refuse, et le champ est vide");
        check(type(*gh, "github.passerelle.jeton", fake.requestToken) && click(*gh, "github.passerelle.jeton.enregistrer"), "le jeton des demandes");
        check(rig.last() && rig.last()->ok && fx.ws->passerelle().requestToken && field->text().empty(), "enregistre, le champ vide");
        check(!has(rig.last()->text(), fake.requestToken), "le jeton n'est pas dans le message");
        check(click(*gh, "github.passerelle.verifier"), "Verifier");
        t = paintPage(*gh, 1280.f, 4000.f);
        check(rig.last() && rig.last()->ok && has(t, "Passerelle v\xC3\xA9rifi\xC3\xA9" "e"), "verifiee : " + (rig.last() ? rig.last()->text() : std::string()));
        check(click(*gh, "github.passerelle.publier"), "Publier la configuration");
        t = paintPage(*gh, 1280.f, 4000.f);
        check(rig.last() && rig.last()->ok && fake.files.count(gw::kConfigPath) && has(t, "Configuration de la passerelle publi\xC3\xA9" "e"), "publiee, montree");
        check(!has(t, fake.requestToken) && !has(t, fake.adminToken), "aucun jeton a l'ecran");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- relever les demandes\n");
    const std::string shared = "XPG-2026-00201";
    {
        page->refresh();
        std::string t = paintPage(*page);
        check(has(t, "jamais relev\xC3\xA9" "es") && has(t, "Pas encore relev\xC3\xA9" "es"), "jamais relevees : dit");
        check(shown(*page, "demandes.relever") && !shown(*page, "demandes.regler"), "Relever les demandes");
        const std::string reqX = fx.request("pc-x", "Poste essai X");
        check(fx.licence(shared, "Client d'essai Beta", "PROFESSIONAL", {reqX}).ok, "une licence existante (par fichier) pour Beta");
        (void)fx.ws->takeGatewayJob();   // (sa publication : pas l'objet de cette section)
    }
    Pc pcA = ask(fx, fake, editor, "A", "Client d'essai Gamma");
    Pc pcB = ask(fx, fake, editor, "B", "Client d'essai Beta", shared);
    {
        check(pcA.issue > 0 && pcB.issue > 0, "deux PC demandent (A : une licence neuve ; B : la licence " + shared + ")");
        check(click(*page, "demandes.relever"), "Relever les demandes");
        const std::string t = paintPage(*page);
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "2 en attente"), "releve : " + (rig.last() ? rig.last()->text() : std::string()));
        check(rows(*page, "demandes.table") == 2, "deux lignes");
        check(has(t, "Client d'essai Gamma") && has(t, "Client d'essai Beta") && has(t, "Poste essai A") && has(t, "API Studio 1.34.0"), "client, PC, logiciel");
        auto* table = find<ui::TableView>(*page, "demandes.table");
        check(table && rowOf(*table, 4, shared + " (ajouter ce PC)") >= 0 && rowOf(*table, 4, "nouvelle") >= 0, "la licence demandee (une partagee le dit)");
        check(has(t, "En attente") && has(t, "Message d'essai de A"), "l'etat, le message");
        check(has(t, "relev\xC3\xA9" "es le " + gui::dateTimeFr(g_now)) && has(t, "2 en attente"), "la date du releve, le compte");
        auto dash = gui::makeDashboardPage(h);
        const std::string dt = paintPage(*dash);
        check(fx.ws->dashboard().onlinePending == 2 && has(dt, "dont 2 en ligne") && has(dt, gui::dateTimeFr(g_now).substr(0, 5) + gui::dateTimeFr(g_now).substr(10)), "le tableau de bord : le compte et sa date");
        if (std::getenv("XPG_DUMP")) std::printf("%s\n", dt.c_str());
        gw::DeviceStatus s;
        check(statusOf(fake, pcA.device, s) && s.state == gw::State::Received, "A voit << recue >>");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- accepter : l'assistant, signer et publier\n");
    {
        check(selectRow(*page, 1, "Client d'essai Gamma"), "la demande de A choisie");
        const std::size_t dialogs = rig.dialogs.size();
        check(click(*page, "demandes.accepter") && rig.dialogs.size() == dialogs + 1, "Accepter... ouvre l'assistant");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Nouvelle licence") && rows(w, "wizard.demandes") == 1 && has(t, "Poste essai A"), "l'assistant : la demande de A");
        check(walkToSign(d, true), "jusqu'a l'etape 6");
        auto* sign = find<ui::Button>(w, "wizard.signer");
        t = paintDialog(d);
        check(sign && sign->text() == "Signer et publier" && has(t, "Aucun fichier \xC3\xA0 envoyer"), "<< Signer et publier >> : rien a envoyer");
        check(find<ui::InputText>(w, "wizard.client") && find<ui::InputText>(w, "wizard.client")->text() == "Client d'essai Gamma", "le client prerempli");
        const std::size_t asked = rig.asked.size();
        g_now += 60;
        check(click(w, "wizard.signer"), "Signer et publier");
        check(rig.asked.size() == asked, "aucun fichier demande");
        gw::DeviceStatus s;
        check(statusOf(fake, pcA.device, s) && s.state == gw::State::Accepted && s.requestNonce == pcA.nonce, "publiee : A << acceptee >>");
        const auto* issue = fake.findIssue(pcA.issue);
        check(issue && issue->state == "closed", "l'issue de A fermee");
        t = paintDialog(d);
        check(has(t, "publi\xC3\xA9" "e sur la passerelle") && has(t, "Rien d'autre \xC3\xA0 faire"), "l'assistant le dit");
        check(rig.last() && rig.last()->ok && has(rig.last()->message, "publi\xC3\xA9" "e sur la passerelle"), "le resultat de la publication est rapporte");
        check(!fx.ws->hasGatewayJob(), "plus rien a publier");
        check(click(w, "wizard.fermer"), "Fermer");
        fx.ws->reload();   // ce que fait la coquille apres un resultat
        page->refresh();
        check(rows(*page, "demandes.table") == 1, "A n'est plus en attente");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une licence partagee : l'assistant en ajout de PC\n");
    {
        check(selectRow(*page, 1, "Client d'essai Beta"), "la demande de B choisie");
        check(click(*page, "demandes.accepter"), "Accepter...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Ajouter des PC \xC3\xA0 la licence " + shared) && rows(w, "wizard.pc.existants") == 1, "en ajout de PC a " + shared);
        check(walkToSign(d, false), "jusqu'a l'etape 6");
        g_now += 60;
        check(click(w, "wizard.signer"), "Signer et publier");
        const gui::LicenceEntry* l = fx.ws->licence(shared);
        check(l && l->licence.devices.size() == 2, "deux PC sur la licence");
        gw::DeviceStatus s;
        check(statusOf(fake, pcB.device, s) && s.state == gw::State::Accepted && s.licenseId == shared, "B << acceptee >>");
        int updated = 0;
        for (const auto& [path, content] : fake.files)
            if (path.rfind("postes/", 0) == 0 && has(content, "mise_a_jour")) ++updated;
        check(updated == 1, "l'autre PC de la licence : << mise_a_jour >>");
        (void)click(w, "wizard.fermer");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- refuser, voir sur GitHub, un travail en cours\n");
    Pc pcC = ask(fx, fake, editor, "C", "Client d'essai Delta");
    {
        check(click(*page, "demandes.relever") && rows(*page, "demandes.table") == 1, "C relevee");
        check(selectRow(*page, 1, "Client d'essai Delta"), "la demande de C choisie");
        check(click(*page, "demandes.github") && !urls.empty() && urls.back() == "https://github.com/" + fake.repo + "/issues/" + std::to_string(pcC.issue),
              "Voir sur GitHub : l'issue");
        check(gui::safeGithubUrl(urls.back()) && !gui::safeGithubUrl("https://exemple.invalid/x") && !gui::safeGithubUrl("http://github.com/a/b") &&
                  !gui::safeGithubUrl("https://github.com/a/../b") && !gui::safeGithubUrl("https://github.com/a/b?x=1"),
              "seules les adresses de GitHub s'ouvrent");
        startBusy(rig);
        const std::size_t before = rig.reports.size();
        check(!click(*page, "demandes.relever") || (rig.reports.size() > before && !rig.reports.back().ok), "un travail en cours : Relever refuse");
        page->refresh();
        check(!find<ui::Button>(*page, "demandes.accepter")->enabled() && !find<ui::Button>(*page, "demandes.relever")->enabled(), "les boutons attendent");
        endBusy(rig);
        page->refresh();
        check(click(*page, "demandes.refuser"), "Refuser...");
        auto& d = *rig.dialogs.back();
        ui::Widget& w = *d.widgetRoot();
        std::string t = paintDialog(d);
        check(has(t, "Refuser la demande") && has(t, "Client d'essai Delta") && has(t, "scell\xC3\xA9" "e"), "la raison est demandee, et ou elle va");
        check(click(w, "raison.ok"), "Refuser sans raison");
        t = paintDialog(d);
        check(has(t, "Donne une raison") && w.visible(), "sans raison : le dialogue reste, et le dit");
        check(type(w, "raison.texte", "Pas de contrat pour ce poste (essai)") && click(w, "raison.ok"), "Refuser avec une raison");
        gw::DeviceStatus s;
        check(statusOf(fake, pcC.device, s) && s.state == gw::State::Refused, "C << refusee >>");
        check(!fake.anyPublicContains("Pas de contrat"), "la raison n'est pas en clair");
        page->refresh();
        t = paintPage(*page);
        check(rows(*page, "demandes.table") == 0 && has(t, "Aucune demande n'attend"), "plus rien en attente : dit");
        auto* filter = find<ui::DropDown>(*page, "demandes.filtre");
        filter->setSelectedIndex(1);
        t = paintPage(*page);
        check(rows(*page, "demandes.table") == 3 && has(t, "Accept\xC3\xA9" "e") && has(t, "Refus\xC3\xA9" "e"), "Toutes : acceptees et refusees");
        check(selectRow(*page, 1, "Client d'essai Gamma") && !find<ui::Button>(*page, "demandes.accepter")->enabled(), "une demande traitee : pas d'Accepter");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une revocation publiee (publishToGateway)\n");
    {
        g_now += 60;
        fx.ws->reload();
        check(fx.ws->revoke(shared, "", "Fin de contrat (essai)", false).ok && fx.ws->hasGatewayJob(), "revoquee : une publication attend");
        startBusy(rig);
        check(!gui::publishToGateway(h) && fx.ws->hasGatewayJob(), "un travail en cours : elle attend (rien de perdu)");
        endBusy(rig);
        check(gui::publishToGateway(h) && rig.last() && rig.last()->ok, "puis elle part : " + (rig.last() ? rig.last()->text() : std::string()));
        gw::DeviceStatus s;
        check(statusOf(fake, pcB.device, s) && s.state == gw::State::Revoked && fake.files.count(gw::kRevocationsPath), "B << revoquee >>, la liste publiee");
        check(!gui::publishToGateway(h), "plus rien a publier");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- hors ligne : dit, jamais << publiee >>\n");
    {
        Pc pcD = ask(fx, fake, editor, "D", "Client d'essai Epsilon");
        fake.offline = true;
        check(click(*page, "demandes.relever") && rig.last() && !rig.last()->ok && has(rig.last()->message, "hors ligne"), "Relever hors ligne : refuse, et dit");
        fake.offline = false;
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le kit des Studios (Parametres de securite)\n");
    {
        auto sec = gui::makeSecurityPage(h);
        const std::string t = paintPage(*sec, 1280.f, 4000.f);
        check(has(t, "Les fichiers pour les Studios") && shown(*sec, "securite.kit-studios"), "la carte et son bouton");
        rig.picks = {U(fx.root / "kit")};
        check(click(*sec, "securite.kit-studios") && rig.last() && !rig.last()->ok && has(rig.last()->message, "ESSAI"), "une cle d'essai : refusee");
        check(!fs::exists(fx.root / "kit" / "PublisherKey.hpp"), "rien d'ecrit");
    }

    fakegh::install(nullptr);
    return finish();
}
