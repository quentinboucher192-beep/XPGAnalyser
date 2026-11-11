// =============================================================================
//  tests/admin_gui_coherence_test.cpp - 1.48.1 : SUPPRIMER, ET QUE CA SE VOIE
//  PARTOUT (<< dans tous les onglets avoir supprimer et que ca mette a jour
//  partout >>)
// -----------------------------------------------------------------------------
//  Un petit monde fictif (cle d'ESSAI, registre d'exemple, clients << Atelier
//  Exemple >>, << Societe d'essai >>, << Brasserie d'essai >>, des PC << Poste
//  essai A... >>), la vraie coquille (Shell : ses pages, report qui relit tout),
//  puis UNE SUPPRESSION DE CHAQUE SORTE, par les boutons des pages :
//    - deux licences d'un coup (une revoquee, une finie) : Licences > Supprimer ;
//    - une licence en vigueur : Supprimer grise, la raison dans l'infobulle ;
//    - une demande notee restee sans suite : Activations > Supprimer ;
//    - un client (sa licence finie) : Clients > Supprimer le client ;
//    - une licence EN LIGNE revoquee (le vrai service local, s'il demarre) :
//      effacee de l'archive ET du service.
//  Apres CHAQUE suppression, chaque page est relue telle que la coquille l'a
//  rafraichie, puis ouverte et dessinee : rien de supprime n'y apparait (sauf
//  l'histoire : Historique, Revocations, Journal), et chaque compte (le tableau
//  de bord, << N sur N >> des barres) est celui des listes.
//  argv[1] : la racine du depot.
// =============================================================================
#include "admin_gui_harness.hpp"
#include "service_local.hpp"

#include "../src/license_admin/gui/Shell.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/DataViews.hpp"
#include "../src/ui/widgets/FilterBar.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

using namespace admintest;
namespace online = license_admin::online;
using license_admin::KnownDevice;

namespace {

const std::string kAtelier = "Atelier Exemple";
const std::string kSociete = "Soci\xC3\xA9t\xC3\xA9 d'essai";
const std::string kBrasserie = "Brasserie d'essai";

std::string paintAll(menu::MenuManager& menus, TextRenderer& r, float w = 1500.f, float h = 950.f) {
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

struct Rig {
    menu::MenuManager menus{menu::MenuFactory{}};
    gui::Shell*       shell{nullptr};
    TextRenderer      r;
    explicit Rig(const Fixture& fx) {
        auto owned = std::make_unique<gui::Shell>(U(fx.admin), [] { return g_now; });
        shell = owned.get();
        menus.PushMenu(std::move(owned));
        menus.applyPending();
    }
    std::string paint() { return paintAll(menus, r); }
    ui::Widget& root() { return *shell->widgetRoot(); }
    bool clickId(const std::string& id) { return click(root(), id); }
    gui::Workspace& ws() { return shell->workspace(); }
    // La question ouverte par-dessus (une confirmation) : << confirmer.ok >>.
    bool confirm(const std::string& expectInText = {}) {
        menus.applyPending();
        auto* d = dynamic_cast<menu::WidgetMenu*>(menus.top());
        if (!d || d == shell || !d->widgetRoot()) return false;
        if (!expectInText.empty()) {
            const std::string t = paint();
            check(has(t, expectInText), "la question dit << " + expectInText + " >>");
        }
        const bool ok = click(*d->widgetRoot(), "confirmer.ok");
        menus.applyPending();
        return ok;
    }
    [[nodiscard]] bool lastOk() const { return shell->lastMessage() && shell->lastMessage()->result.ok; }
    [[nodiscard]] std::string lastText() const { return shell->lastMessage() ? shell->lastMessage()->result.text() : std::string("(rien)"); }
};

ui::TableView* tableOf(gui::Shell& s, const std::string& page, std::string_view id) {
    gui::Page* p = s.page(page);
    return p ? find<ui::TableView>(*p, id) : nullptr;
}

bool selectRows(ui::TableView* t, std::size_t col, const std::vector<std::string>& texts) {
    if (!t || !t->model()) return false;
    std::vector<ui::RowIndex> rows;
    for (std::size_t r = 0; r < t->model()->rowCount(); ++r)
        for (const auto& x : texts)
            if (t->model()->cellText(static_cast<ui::RowIndex>(r), col) == x) rows.push_back(static_cast<ui::RowIndex>(r));
    if (rows.size() != texts.size()) return false;
    t->selectModelRows(rows, true);
    return true;
}

bool tableHas(ui::TableView* t, const std::string& text) {
    if (!t || !t->model()) return false;
    for (std::size_t r = 0; r < t->model()->rowCount(); ++r)
        for (std::size_t c = 0; c < t->model()->columnCount(); ++c)
            if (has(t->model()->cellText(static_cast<ui::RowIndex>(r), c), text)) return true;
    return false;
}

// APRES UNE SUPPRESSION : chaque page, telle que la coquille l'a rafraichie (sans y aller), puis ouverte et dessinee.
// `gone` : ce qui ne doit plus apparaitre nulle part (hors histoire).
void sweep(Rig& rig, const std::string& step, const std::vector<std::string>& gone) {
    gui::Shell& s = *rig.shell;
    while (s.finishSlow()) {}
    gui::Workspace& w = rig.ws();
    const auto devices = w.devices();
    const auto clients = w.clients();
    // 1. les listes, avant d'y aller (la coquille les a deja rafraichies : report)
    struct ListCheck {
        const char* page;
        const char* table;
        const char* bar;
        std::size_t expected;
    };
    const ListCheck lists[] = {
        {"licences", "licences.table", "licences.barre", w.licences().size()},
        {"activations", "activations.table", "activations", devices.size()},
        {"clients", "clients.table", "clients", clients.size()},
    };
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& l : lists) {
            if (pass == 1) {
                s.navigate(l.page);
                (void)rig.paint();
            }
            gui::Page* p = s.page(l.page);
            if (!p) continue;   // pas encore ouverte : la deuxieme passe
            auto* bar = find<ui::FilterBar>(*p, l.bar);
            auto* t = find<ui::TableView>(*p, l.table);
            const std::string where = step + " - " + l.page + (pass ? " (ouverte)" : " (rafraichie)");
            check(bar && bar->totalCount() == l.expected,
                  where + " : << sur " + std::to_string(l.expected) + " >> (" + (bar ? bar->countText() : std::string("pas de barre")) + ")");
            if (bar && t && std::string(l.page) != "clients")   // Clients : un arbre (les licences des clients deplies en plus)
                check(t->model() && t->model()->rowCount() == bar->shownCount(), where + " : les lignes de la table = " + bar->countText());
            for (const auto& g : gone) check(!tableHas(t, g), where + " : plus de << " + g + " >>");
        }
    }
    // 2. le tableau de bord : ses chiffres sont ceux des listes
    const gui::Dashboard d = w.dashboard();
    int active = 0, waiting = 0, revoked = 0;
    for (const auto& x : devices) {
        active += x.state == "active" ? 1 : 0;
        waiting += x.state == "demande" ? 1 : 0;
    }
    for (const auto& l : w.licences()) revoked += l.revoked ? 1 : 0;
    check(d.licences == static_cast<int>(w.licences().size()) && d.clients == static_cast<int>(clients.size()) && d.devices == active &&
              d.pendingRequests == waiting && d.revokedLicences == revoked,
          step + " - le tableau de bord compte comme les listes (licences " + std::to_string(d.licences) + ", clients " + std::to_string(d.clients) + ", PC " +
              std::to_string(d.devices) + "/" + std::to_string(active) + ", demandes " + std::to_string(d.pendingRequests) + "/" + std::to_string(waiting) + ")");
    check(d.onlineLicences == static_cast<int>(w.service().snapshot.licences.size()), step + " - le tableau de bord : les licences en ligne du releve");
    for (const auto& c : clients) {
        for (const auto& id : c.licenceIds)
            for (const auto& g : gone) check(id != g, step + " - le client " + c.name + " ne compte plus " + g);
        for (const auto& g : gone) check(c.name != g, step + " - plus de client " + g);
    }
    for (const auto& x : devices)
        for (const auto& g : gone) check(x.licenseId != g && x.name != g && x.customer != g, step + " - Activations : plus de " + g);
    for (const auto& l : w.service().snapshot.licences)
        for (const auto& g : gone) check(l.licenseId != g, step + " - le releve du service : plus de " + g);
    // 3. chaque page dessinee (sauf l'histoire : Historique, Revocations, Journal des admins)
    s.flash("Relu.", gui::Tone::Info);   // le bandeau du dernier resultat (qui nomme ce qui vient d'etre supprime) : pas la page
    for (const char* page : {"tableau", "licences", "clients", "activations", "flottantes"}) {
        s.navigate(page);
        const std::string t = rig.paint();
        for (const auto& g : gone) {
            const bool ok = !has(t, g);
            check(ok, step + " - la page " + page + " ne montre plus << " + g + " >>");
            if (!ok && std::getenv("XPG_DEBUG_COHERENCE"))
                for (const auto& d : rig.r.draws)
                    if (has(d.text, g)) std::printf("      [%s] %s\n", page, d.text.c_str());
        }
    }
    // la fiche d'un client qui reste (ses licences, ses PC)
    gui::requestPageFocus("client", "client:" + kAtelier);
    s.navigate("client");
    const std::string card = rig.paint();
    check(has(card, kAtelier), step + " - la fiche de " + kAtelier);
    for (const auto& g : gone) check(!has(card, g), step + " - la fiche de " + kAtelier + " ne montre plus << " + g + " >>");
}

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    installMetrics();
    g_now = static_cast<lic::Seconds>(std::time(nullptr));   // le service local signe a l'heure vraie
    const lic::Seconds today = g_now;

    Fixture fx(source, "coherence");
    check(fx.ready(), "le poste d'essai (cle d'essai, registre d'exemple)");
    const std::string L1 = "XPG-2026-00101", L2 = "XPG-2026-00102", L3 = "XPG-2026-00103", L4 = "XPG-2026-00104", O1 = "XPG-2026-00105";
    {   // le monde : deux licences finies (signees il y a 3 jours pour 1 jour), deux en vigueur, une demande notee
        g_now = today - 3 * 86400;
        check(fx.licence(L3, kSociete, "PERSONAL", {fx.request("pc-s", "Poste essai S")}, 1).ok, "L3 : la licence (finie) de " + kSociete);
        check(fx.licence(L4, kBrasserie, "PERSONAL", {fx.request("pc-e", "Poste essai E")}, 1).ok, "L4 : la licence (finie) de " + kBrasserie);
        g_now = today;
        check(fx.licence(L1, kAtelier, "PROFESSIONAL", {fx.request("pc-a", "Poste essai A"), fx.request("pc-b", "Poste essai B")}).ok, "L1 : deux PC");
        check(fx.licence(L2, kAtelier, "PROFESSIONAL", {fx.request("pc-c", "Poste essai C")}).ok, "L2 : un PC");
        check(fx.ws->recordRequests({fx.request("pc-d", "Poste essai D")}).ok, "une demande notee (Poste essai D)");
        check(fx.ws->revoke(L1, {}, "Fin de contrat (essai)", false).ok, "L1 revoquee");
    }

    // ---- le vrai service local (une licence en ligne) : s'il ne demarre pas, la suite se fait sans lui ----
    const lic::SigningKey* editorKey = fx.ws->signingKey();
    xpg_test::LocalService::Options so;
    so.sourceDir = source;
    std::unique_ptr<xpg_test::LocalService> svc;
    if (editorKey) svc = std::make_unique<xpg_test::LocalService>(std::vector<std::string>{lic::crypto::hex(editorKey->publicKey())}, so);
    const bool online = svc && svc->ready();
    if (!online) std::printf("SAUTE (la partie en ligne) : %s\n", svc ? svc->error().c_str() : "pas de cle");
    if (online) {
        check(fx.ws->serviceConfigure(svc->url()).ok, "le service local regle");
        std::string keyHex;
        check(fx.ws->serviceReadKey(keyHex).ok && fx.ws->serviceCertify(keyHex).ok, "la cle du service certifiee");
        gui::ServiceJob reg;
        reg.kind = gui::ServiceJob::Kind::Registry;
        check(fx.ws->serviceRun(reg).ok, "le registre depose");
        gui::LicenceDraft d;
        d.online = true;
        d.licenseId = O1;
        d.customer = kAtelier;
        d.edition = "PROFESSIONAL";
        gui::ServiceJob job;
        std::string code;
        gui::OpResult r = fx.ws->prepareOnlineLicence(d, {}, job, code);
        check(r.ok && fx.ws->serviceRunOnline(job, code).ok, "O1 : la licence en ligne deposee (" + r.text() + ")");
        check(fx.ws->revoke(O1, {}, "Essai", false).ok, "O1 revoquee");
        if (auto j = fx.ws->takeServiceJob()) check(fx.ws->serviceRun(*j).ok, "la revocation de O1 deposee");
        online::Snapshot snap;
        check(fx.ws->serviceListLicences(snap).ok && snap.licence(O1), "O1 est sur le service");
        fx.ws->adoptSnapshot(snap);
        (void)online::writeSnapshot(U(fx.admin), snap, nullptr);
    } else {
        (void)fx.ws->takeServiceJob();
    }

    Rig rig(fx);
    gui::Shell& s = *rig.shell;
    for (const auto& e : gui::Shell::entries()) {   // chaque page ouverte une fois (elles gardent ce qu'elles montrent)
        s.navigate(e.id);
        (void)rig.paint();
    }
    sweep(rig, "le monde de depart", {});
    check(rig.ws().licences().size() == 4u + (online ? 1u : 0u), "les licences de depart");

    // =========================================================== une licence en vigueur : Supprimer grise, la raison
    std::printf("-- une licence en vigueur ne se supprime pas (le bouton est la, grise, il dit pourquoi)\n");
    {
        s.navigate("licences");
        (void)rig.paint();
        auto* t = tableOf(s, "licences", "licences.table");
        check(selectRows(t, 0, {L2}), "L2 choisie");
        auto* del = find<ui::Button>(*s.page("licences"), "licences.supprimer");
        check(del && !del->enabled() && has(del->tooltip(), "en vigueur") && has(del->tooltip(), "r\xC3\xA9voque-la"),
              "Supprimer grise : << " + (del ? del->tooltip() : std::string()) + " >>");
        check(!rig.ws().deleteLicences({L2}).ok && rig.ws().licence(L2), "le Workspace refuse aussi (L2 reste)");
        check(!rig.ws().deleteLicences({L1, L2}).ok && rig.ws().licence(L1), "une seule en vigueur dans le lot : rien n'est supprime");
        auto* act = tableOf(s, "activations", "activations.table");
        s.navigate("activations");
        (void)rig.paint();
        check(selectRows(act, 0, {"Poste essai C"}), "Poste essai C (actif) choisi");
        auto* pd = find<ui::Button>(*s.page("activations"), "activations.supprimer");
        check(pd && !pd->enabled() && has(pd->tooltip(), "retire-le de sa licence"), "Activations : Supprimer grise pour un PC actif : << " + (pd ? pd->tooltip() : "") + " >>");
        for (const char* page : {"historique", "journal", "versions"}) {
            s.navigate(page);
            (void)rig.paint();
        }
        auto* hd = find<ui::Button>(*s.page("historique"), "historique.supprimer");
        check(hd && !hd->enabled() && has(hd->tooltip(), "ne s'efface pas"), "Historique : Supprimer la, grise, avec sa raison");
        if (gui::Page* jp = s.page("journal")) {
            auto* jd = find<ui::Button>(*jp, "journal.supprimer");
            check(jd && !jd->enabled() && has(jd->tooltip(), "cha\xC3\xAEne"), "Journal des admins : Supprimer la, grise, avec sa raison");
        }
        if (gui::Page* vp = s.page("versions")) {
            auto* vd = find<ui::Button>(*vp, "versions.supprimer");
            check(vd && !vd->enabled() && has(vd->tooltip(), "publi\xC3\xA9" "e"), "Versions : Supprimer la, grise, avec sa raison");
        }
        for (const char* id : {"licences.actualiser", "activations.actualiser", "clients.actualiser", "editions.actualiser", "options.actualiser",
                               "fonctionnalites.actualiser", "revocations.actualiser", "historique.actualiser"}) {
            const std::string page = std::string(id).substr(0, std::string(id).find('.'));
            s.navigate(page);
            (void)rig.paint();
            gui::Page* p = s.page(page);
            check(p && rig.clickId(id) && rig.lastOk(), std::string("Actualiser : ") + id + " (" + rig.lastText() + ")");
            while (rig.shell->finishSlow()) {}   // le releve des licences en ligne (le service regle)
        }
    }

    // =========================================================== deux licences d'un coup : une revoquee, une finie
    std::printf("-- Licences > Supprimer : L1 (revoquee) et L3 (finie) d'un coup\n");
    {
        s.navigate("licences");
        (void)rig.paint();
        auto* t = tableOf(s, "licences", "licences.table");
        check(selectRows(t, 0, {L1, L3}), "L1 et L3 choisies");
        auto* del = find<ui::Button>(*s.page("licences"), "licences.supprimer");
        check(del && del->enabled(), "Supprimer permis (" + (del ? del->tooltip() : std::string()) + ")");
        check(rig.clickId("licences.supprimer") && rig.confirm("quittent Activations"), "Supprimer, puis la question (ce qui part, ce qui suit)");
        check(rig.lastOk() && !rig.ws().licence(L1) && !rig.ws().licence(L3), "supprimees : " + rig.lastText());
        sweep(rig, "L1 et L3 supprimees", {L1, L3, "Poste essai A", "Poste essai B", "Poste essai S"});
        // l'histoire reste : la revocation de L1, l'historique
        s.navigate("revocations");
        (void)rig.paint();
        check(tableHas(tableOf(s, "revocations", "revocations.table"), L1), "Revocations : la revocation de L1 reste (elle protege toujours)");
        s.navigate("historique");
        check(has(rig.paint(), L1), "Historique : L1 y reste (l'histoire)");
    }

    // =========================================================== une demande notee restee sans suite : Activations
    std::printf("-- Activations > Supprimer : la demande de Poste essai D\n");
    {
        s.navigate("activations");
        (void)rig.paint();
        auto* act = tableOf(s, "activations", "activations.table");
        check(selectRows(act, 0, {"Poste essai D"}), "Poste essai D choisi");
        check(rig.clickId("activations.supprimer") && rig.confirm("l'Historique garde tout"), "Supprimer, la question");
        check(rig.lastOk(), "supprime : " + rig.lastText());
        sweep(rig, "Poste essai D supprime", {L1, L3, "Poste essai D"});
    }

    // =========================================================== un client (sa licence finie) : Clients
    std::printf("-- Clients > Supprimer le client : %s\n", kBrasserie.c_str());
    {
        s.navigate("clients");
        (void)rig.paint();
        auto* ct = tableOf(s, "clients", "clients.table");
        check(selectRows(ct, 0, {kBrasserie}), kBrasserie + " choisi");
        check(rig.clickId("clients.supprimer") && rig.confirm("corbeille"), "Supprimer le client, la question");
        check(rig.lastOk() && !rig.ws().licence(L4), "supprime : " + rig.lastText());
        sweep(rig, kBrasserie + " supprime", {L1, L3, L4, "Poste essai D", "Poste essai E", kBrasserie});
    }

    // =========================================================== une licence en ligne revoquee : l'archive ET le service
    if (online) {
        std::printf("-- une licence en ligne revoquee : Licences > Supprimer, puis le service l'efface\n");
        s.navigate("licences");
        (void)rig.paint();
        auto* t = tableOf(s, "licences", "licences.table");
        check(selectRows(t, 0, {O1}), "O1 choisie");
        const int before = rig.ws().dashboard().onlineLicences;
        check(rig.clickId("licences.supprimer") && rig.confirm("du service"), "Supprimer, la question dit le service");
        check(rig.lastOk() && !rig.ws().licence(O1), "O1 supprimee de l'archive : " + rig.lastText());
        check(rig.ws().dashboard().onlineLicences == before - 1, "le tableau de bord ne la compte plus, avant meme le service");
        (void)rig.paint();             // la coquille lance la publication en attente (Update)
        (void)rig.shell->finishSlow();
        check(rig.lastOk() && has(rig.lastText(), "1 licence effac\xC3\xA9" "e"), "le service l'a effacee : " + rig.lastText());
        std::printf("   (%s)\n", rig.lastText().c_str());
        online::Snapshot snap;
        check(fx.ws->serviceListLicences(snap).ok && !snap.licence(O1), "O1 n'est plus sur le service (le releve brut)");
        sweep(rig, "O1 supprimee", {L1, L3, L4, O1, "Poste essai D", kBrasserie});
    }

    // ce qui reste : L2 et son PC, intacts
    check(rig.ws().licence(L2) != nullptr, "L2 (en vigueur) reste");
    const auto devs = rig.ws().devices();
    check(std::any_of(devs.begin(), devs.end(), [](const KnownDevice& d) { return d.name == "Poste essai C" && d.state == "active"; }), "Poste essai C reste actif");
    return finish();
}
