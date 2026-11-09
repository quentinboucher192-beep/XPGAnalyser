// =============================================================================
//  tests/admin_gui_shell_test.cpp - 1.33.0 : LA COQUILLE de XPG-License-Manager-Admin
//  (le menu, la barre du haut, le bandeau, le travail lent, les fichiers glisses),
//  le TABLEAU DE BORD, les deux pages de la VENTE EN LIGNE (pas encore) et, 1.35.0,
//  la vraie page des LICENCES FLOTTANTES (un releve du service), dessines a blanc.
//  argv[1] : la racine du depot (le registre d'exemple).
// =============================================================================
#include "admin_gui_harness.hpp"

#include "../src/license_admin/gui/Shell.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/widgets/DataViews.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

using namespace admintest;
namespace online = license_admin::online;

namespace {

// Toute la pile (la coquille et un dialogue ouvert), comme la fenetre.
std::string paintAll(menu::MenuManager& menus, TextRenderer& r, float w = 1400.f, float h = 900.f) {
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
    std::string paint(float w = 1400.f, float h = 900.f) { return paintAll(menus, r, w, h); }
    ui::Widget& root() { return *shell->widgetRoot(); }
    bool clickId(const std::string& id) { return click(root(), id); }
};

// 1.34.0 : << Demandes en ligne >> apres Activations. 1.35.0 : << Service en ligne >> a la place de << Synchronisation
// GitHub >> ; << Licences flottantes >> est une vraie page (seules Boutique et Commandes restent grisees).
// 1.41.0 : les groupes de la maquette validee (Pilotage, Catalogue, la vente en ligne, Securite).
// 1.42.0 : les editions s'appellent les Offres ; les Options a leur suite.
// 1.44.0 : plusieurs admins - Administrateurs, Journal des admins et Versions dans Securite (ces deux-la : l'admin+ seul).
const std::string kTitles[] = {"Tableau de bord", "Licences", "Clients", "Demandes en ligne", "Activations", "Offres", "Options", "Fonctionnalit\xC3\xA9s", "Licences flottantes",
                               "Boutique", "Commandes", "R\xC3\xA9vocations", "Administrateurs", "Journal des admins", "Versions", "Service en ligne", "Historique",
                               "Param\xC3\xA8tres de s\xC3\xA9" "curit\xC3\xA9"};
constexpr std::size_t kEntries = sizeof(kTitles) / sizeof(kTitles[0]);

} // namespace

int main(int argc, char** argv) {
    const std::string source = argc > 1 ? argv[1] : ".";
    installMetrics();

    // ------------------------------------------------------------------------------------------------
    std::printf("-- premier lancement : aucune cle\n");
    {
        Fixture fx(source, "coquille-vide");
        Rig rig(fx);
        const std::string t = rig.paint();
        check(has(t, "XPG License Manager") && has(t, "Admin"), "le nom de l'application dans le menu");
        check(has(t, "\xC3\x89" "diteur seulement"), "<< Editeur seulement >>");
        const auto& entries = gui::Shell::entries();
        check(entries.size() == kEntries, std::to_string(kEntries) + " entrees dans le menu");
        for (std::size_t i = 0; i < entries.size() && i < kEntries; ++i) {
            check(entries[i].label == kTitles[i], std::string("l'ordre de la maquette : ") + kTitles[i]);
            auto* nav = find<ui::Button>(rig.root(), std::string("nav.") + entries[i].id);
            check(nav != nullptr, std::string("nav.") + entries[i].id);
            // 1.44.0 : sans cle, les entrees de l'admin+ seul (le journal des admins, les versions) ne sont pas dans le menu.
            if (gui::Shell::plusOnly(entries[i].id)) check(nav && !nav->visible(), std::string("le menu cache ") + kTitles[i] + " (l'admin+ seul)");
            else check(has(t, kTitles[i]), std::string("le menu montre ") + kTitles[i]);
        }
        int online = 0;
        for (const auto& e : entries) online += e.online ? 1 : 0;
        check(online == 2 && !gui::Shell::entry("flottantes")->online && gui::Shell::entry("boutique")->online && gui::Shell::entry("commandes")->online,
              "deux entrees grisees (la vente en ligne) ; Licences flottantes est une vraie page");
        check(has(t, "VENTE EN LIGNE") && !has(t, "GitHub"), "le menu dit << vente en ligne >> ; plus aucun GitHub");
        check(gui::Shell::entry("service") && !gui::Shell::entry("github"), "la page << service >> ; plus de page << github >>");
        check(rig.shell->currentPageId() == "tableau", "on ouvre sur le tableau de bord");
        check(has(t, "Pas de cl\xC3\xA9") && rig.shell->keyStatusTone() == gui::Tone::Error, "<< Pas de cle >> en rouge");
        check(has(t, "Service : non r\xC3\xA9gl\xC3\xA9"), "<< Service : non regle >>");
        check(has(t, "Pour commencer") && has(t, "Cr\xC3\xA9" "er la cl\xC3\xA9"), "le tableau de bord dit comment commencer");
        check(has(t, "Pr\xC3\xAAt."), "le bandeau avant toute operation");
        auto* nl = find<ui::Button>(rig.root(), "tableau.nouvelle-licence");
        check(nl && !nl->enabled(), "Nouvelle licence... grisee sans cle");
        auto* nlTop = find<ui::Button>(rig.root(), "shell.nouvelle-licence");
        check(nlTop && !nlTop->enabled(), "1.41.0 : Nouvelle licence (en haut) grisee sans cle");
        check(rig.clickId("tableau.creer-cle") && rig.shell->currentPageId() == "securite", "Creer la cle... ouvre Parametres de securite");
        rig.shell->navigate("tableau");
        check(rig.clickId("shell.cle") && rig.shell->currentPageId() == "securite", "un clic sur l'etat de la cle : Parametres de securite");
        check(rig.clickId("shell.synchro") && rig.shell->currentPageId() == "service", "un clic sur l'etat du service : la page Service en ligne");
        // La plus petite fenetre : le menu tient en entier.
        rig.paint(1100.f, 700.f);
        auto* last = find<ui::Button>(rig.root(), "nav.securite");
        check(last && last->bounds().bottom() <= 700.f, "a 1100 x 700, la derniere entree du menu est visible");
        auto* first = find<ui::Button>(rig.root(), "nav.tableau");
        check(first && first->bounds().right() <= 232.f, "le menu fait 232 px");
        // L'explorateur de fichiers absent : le refus est dit.
        rig.shell->navigate("tableau");
        check(rig.clickId("tableau.noter-demandes"), "Noter des demandes...");
        check(rig.shell->lastMessage() && !rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "explorateur"),
              "sans explorateur de fichiers : refuse, et dit");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- une cle, pas encore de registre\n");
    {
        Fixture fx2(source, "coquille-sans-registre");
        check(fx2.ws->createKey("essai sans registre", true).ok, "cle d'essai creee");
        Rig rig2(fx2);
        const std::string t = rig2.paint();
        check(has(t, "Il manque le registre") && has(t, "Aller aux \xC3\xA9" "ditions"), "le tableau de bord dit de signer le registre");
        auto* nl = find<ui::Button>(rig2.root(), "tableau.nouvelle-licence");
        check(nl && !nl->enabled(), "Nouvelle licence... grisee sans registre");
        check(rig2.clickId("tableau.aller-editions") && rig2.shell->currentPageId() == "editions", "Aller aux editions");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- cle d'essai, registre : navigation et etat\n");
    Fixture fx(source, "coquille");
    check(fx.ready(), "le poste d'essai est pret (cle d'essai, registre)");
    Rig rig(fx);
    {
        const std::string t = rig.paint();
        check(has(t, "CL\xC3\x89 D'ESSAI") && rig.shell->keyStatusTone() == gui::Tone::Warning, "<< CLE D'ESSAI >> (avertissement)");
        check(has(t, fx.ws->key().id), "l'identifiant de la cle");
        check(!has(t, "Pour commencer"), "plus de conseil de premier lancement");
        auto* nl = find<ui::Button>(rig.root(), "tableau.nouvelle-licence");
        check(nl && nl->enabled(), "Nouvelle licence... possible");
        // 1.41.0 : les cadres de la maquette (l'activite, les licences par edition, l'etat du poste).
        check(has(t, "Licences par \xC3\xA9" "dition") && has(t, "\xC3\x89tat du poste") && has(t, "Activit\xC3\xA9"), "les trois cadres du tableau de bord");
        check(has(t, "Aucune op\xC3\xA9ration pour l'instant"), "aucune operation encore");
        check(has(t, "Licences flottantes") && has(t, "Licences en ligne") && has(t, "service non r\xC3\xA9gl\xC3\xA9"),
              "les tuiles du service : sans chiffre tant qu'il n'est pas regle");
        check(has(t, "Finissent dans 30 jours"), "la tuile << Finissent dans 30 jours >>");
        check(has(t, "n\xC2\xB0 " + std::to_string(fx.ws->registry().number)), "le numero du registre");
    }
    for (const auto& e : gui::Shell::entries()) {
        check(rig.clickId(std::string("nav.") + e.id), std::string("clic nav.") + e.id);
        const std::string t = rig.paint();
        check(rig.shell->currentPageId() == e.id && rig.shell->page(e.id) != nullptr, std::string("la page ") + e.id + " est montree");
        check(!rig.shell->pageTitle().empty() && has(t, rig.shell->pageTitle()), std::string("le titre de la page en haut : ") + rig.shell->pageTitle());
        check(rig.shell->currentPage() && rig.shell->currentPage()->visible(), std::string("visible : ") + e.id);
        int shown = 0;
        for (const auto& o : gui::Shell::entries())
            if (auto* p = rig.shell->page(o.id); p && p->visible()) ++shown;
        check(shown == 1, std::string("une seule page a la fois (") + e.id + ")");
    }
    rig.shell->navigate("nulle-part");
    check(rig.shell->currentPageId() == "securite" && rig.shell->lastMessage() && rig.shell->lastMessage()->tone == gui::Tone::Warning,
          "une page inconnue : on reste, et c'est dit");

    // ------------------------------------------------------------------------------------------------
    std::printf("-- la vente en ligne (pas encore) et les licences flottantes (une vraie page)\n");
    {
        const struct {
            const char* id;
            const char* word;
            const char* go;
            const char* to;
        } pages[] = {{"boutique", "prestataire", "en-ligne.boutique.aller", "licences"},
                     {"commandes", "aucune commande", "en-ligne.commandes.aller", "historique"}};
        for (const auto& p : pages) {
            rig.shell->navigate(p.id);
            const std::string t = rig.paint();
            check(has(t, "Pas encore fait") && has(t, "paiement en ligne"), std::string(p.id) + " : << pas encore >> (un paiement)");
            check(has(t, "Rien n'y est simul\xC3\xA9"), std::string(p.id) + " : rien de simule");
            check(has(t, "Ce que fera cette page") && has(t, "Pourquoi ce n'est pas encore fait"), std::string(p.id) + " : ce qu'elle fera, pourquoi");
            check(has(t, p.word), std::string(p.id) + " : " + p.word);
            check(rig.clickId(p.go) && rig.shell->currentPageId() == p.to, std::string(p.id) + " : En attendant -> " + p.to);
            // Etroite : rien ne deborde, la page se lit encore.
            rig.shell->navigate(p.id);
            const std::string n = rig.paint(1100.f, 700.f);
            check(has(n, "Pas encore fait"), std::string(p.id) + " a 1100 x 700");
        }
        rig.shell->navigate("flottantes");
        std::string t = rig.paint();
        check(has(t, "EN M\xC3\x8AME TEMPS") && has(t, "pas de licence flottante sans lui") && !has(t, "Pas encore fait"),
              "flottantes : une vraie page ; le service pas regle, c'est dit");
        auto* fr = find<ui::Button>(rig.root(), "flottantes.relever");
        check(fr && !fr->enabled(), "Relever grise sans service");
        check(rig.clickId("flottantes.regler") && rig.shell->currentPageId() == "service", "Regler le service -> Service en ligne");
        // Un releve (le dernier, garde sur ce poste) : la page le montre, chiffres et date.
        check(rig.shell->workspace().serviceConfigure("https://xpg-licences.essai.workers.dev").ok, "le service regle (sans reseau)");
        online::Snapshot s;
        s.at = g_now;
        s.url = "https://xpg-licences.essai.workers.dev";
        online::OnlineLicence fl;
        fl.licenseId = "XPG-2026-00077";
        fl.type = "floating";
        fl.seats = 2;
        fl.leaseDays = 30;
        fl.hasCode = true;
        online::OnlineActivation a1;
        a1.deviceKey = std::string(64, 'a');
        a1.name = "Poste flottant A";
        a1.state = "active";
        a1.activatedAt = a1.seenAt = g_now - 3600;
        a1.until = g_now + 29 * 86400;
        fl.activations.push_back(a1);
        fl.leases.push_back({a1.deviceKey, g_now + 600});
        s.licences.push_back(fl);
        std::string why;
        check(online::writeSnapshot(U(fx.admin), s, &why), "un releve d'essai garde sur ce poste");
        rig.shell->reloadAll();
        rig.shell->navigate("flottantes");
        t = rig.paint();
        check(has(t, "XPG-2026-00077") && has(t, "1 / 2") && has(t, "1 jeton(s) en cours sur 2 poste(s)"), "flottantes : la licence, les postes pris, le releve");
        check(has(t, "Poste flottant A"), "le PC qui tient le jeton");
        rig.shell->navigate("tableau");
        t = rig.paint();
        check(has(t, "1 / 2") && has(t, "jetons en cours") && has(t, "Licences en ligne"), "le tableau de bord : les chiffres du releve (1.41.0 : 1 / 2 jetons)");
        (void)fs::remove(fx.admin / online::kSnapshotFile);
        (void)fs::remove(fx.admin / online::kConfigFile);
        rig.shell->reloadAll();
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le bandeau et le journal de la seance\n");
    {
        rig.shell->navigate("tableau");
        rig.shell->report(gui::OpResult::fail("essai de refus"));
        std::string t = rig.paint();
        check(has(t, "Refus\xC3\xA9 : essai de refus") && rig.shell->lastMessage()->tone == gui::Tone::Error, "un refus dans le bandeau (rouge)");
        gui::OpResult ok = gui::OpResult::done("Essai fait.");
        ok.details = {"premier detail de l'essai", "second detail de l'essai"};
        rig.shell->report(ok);
        t = rig.paint();
        check(has(t, "Essai fait.") && rig.shell->lastMessage()->tone == gui::Tone::Ok, "un resultat fait (vert)");
        check(!has(t, "premier detail de l'essai"), "les details caches");
        check(has(t, "D\xC3\xA9tails (2)"), "le bouton Details (2)");
        check(rig.clickId("shell.details") && rig.shell->detailsOpen(), "Details : ouverts");
        t = rig.paint();
        check(has(t, "premier detail de l'essai") && has(t, "second detail de l'essai"), "les details montres");
        check(has(t, "Masquer les d\xC3\xA9tails"), "Masquer les details");
        rig.shell->report(gui::OpResult::done("Un autre."));
        check(!rig.shell->detailsOpen(), "un nouveau resultat referme les details");
        check(rig.shell->sessionLog().size() >= 4, "le journal garde tout");
        {
            Fixture fx3(source, "coquille-journal");
            Rig rig3(fx3);
            check(rig3.clickId("shell.journal") && has(rig3.paint(), "Rien encore dans cette s\xC3\xA9" "ance."), "le journal vide le dit");
        }
        check(rig.clickId("shell.journal") && rig.shell->logOpen(), "Journal : ouvert");
        t = rig.paint();
        check(has(t, "Fermer le journal") && has(t, "essai de refus") && has(t, "Essai fait.") && has(t, "    premier detail de l'essai"),
              "le journal montre les resultats et leurs details");
        auto* table = find<ui::TableView>(rig.root(), "shell.journal.table");
        check(table && table->visible() && table->model() && table->model()->rowCount() >= 5, "la table du journal");
        check(table && table->model()->cellText(0, 2) == "Un autre.", "le plus recent en haut");
        check(rig.clickId("shell.journal") && !rig.shell->logOpen(), "Journal : ferme");
        check(rig.clickId("shell.journal") && rig.shell->logOpen(), "Journal : rouvert");
        (void)rig.menus.HandleEvent(ui::KeyDown{ui::Key::Escape, {}, false});
        check(!rig.shell->logOpen() && rig.menus.top() == rig.shell, "Echap referme le journal (la coquille reste)");
        // Le dossier d'un fichier ecrit (un resultat avec output).
        gui::OpResult out = gui::OpResult::done("Fichier \xC3\xA9" "crit.");
        out.output = U(fx.root / "nulle-part" / "x.xpglic");
        rig.shell->report(out);
        rig.paint();
        auto* folder = find<ui::Button>(rig.root(), "shell.ouvrir-dossier");
        check(folder && folder->visibility() == ui::Visibility::Visible, "Ouvrir le dossier (un fichier ecrit)");
        check(rig.clickId("shell.ouvrir-dossier") && !rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "n'existe pas"),
              "un dossier absent : refuse, sans rien lancer");
        rig.shell->flash("Un message d'information.", gui::Tone::Info);
        t = rig.paint();
        check(has(t, "Un message d'information."), "flash dans le bandeau");
        // F5 : tout relire.
        (void)rig.menus.HandleEvent(ui::KeyDown{ui::Key::F5, {}, false});
        check(has(rig.shell->lastMessage()->result.message, "Relu"), "F5 relit tout");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le travail lent sur un autre fil\n");
    {
        std::atomic<bool> release{false};
        bool doneSeen = false;
        const bool started = rig.shell->runSlow("Synchro en cours",
                                                [&release] {
                                                    while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                                    return gui::OpResult::done("Travail lent fini.");
                                                },
                                                [&doneSeen](const gui::OpResult& r) { doneSeen = r.ok; });
        check(started && rig.shell->busy(), "runSlow : le travail tourne (busy)");
        const std::string t = rig.paint();
        check(has(t, "Synchro en cours\xE2\x80\xA6"), "la barre du haut : << Synchro en cours... >>");
        check(!rig.shell->runSlow("Un second", [] { return gui::OpResult::done("non"); }, nullptr), "un second travail lent est refuse");
        // Un fichier glisse sur une page qui n'en veut pas, pendant le travail : refuse.
        rig.shell->navigate("boutique");
        const std::string reqWhileBusy = fx.request("pc-occupe", "Poste essai occup\xC3\xA9");
        rig.shell->dropFiles({reqWhileBusy});
        check(!rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "attends la fin"), "ranger pendant un travail lent : refuse");
        check(rig.shell->workspace().dashboard().pendingRequests == 0, "rien n'a ete note");
        rig.shell->navigate("tableau");
        check(rig.clickId("tableau.noter-demandes") && !rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "travail lent"),
              "Noter des demandes... pendant un travail lent : refuse");
        release.store(true);
        bool finished = false;
        for (int i = 0; i < 500 && !finished; ++i) {
            rig.paint();   // Update suit le travail a chaque image
            finished = !rig.shell->busy() && rig.shell->lastMessage() && rig.shell->lastMessage()->result.message == "Travail lent fini.";
            if (!finished) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        check(finished && doneSeen, "fini : done puis le bandeau (sur le fil de l'interface)");
        check(!has(rig.paint(), "Synchro en cours"), "la barre du haut redit l'etat du service");
        // finishSlow (les essais, la fermeture) : le meme rendu.
        check(rig.shell->runSlow("Encore", [] { return gui::OpResult::fail("lent et refuse"); }, nullptr) && rig.shell->finishSlow(),
              "finishSlow rend le resultat");
        check(has(rig.shell->lastMessage()->result.message, "lent et refuse"), "le refus du travail lent dans le bandeau");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- les fichiers glisses\n");
    {
        rig.shell->navigate("flottantes");
        const std::string req = fx.request("pc-b", "Poste essai B");
        rig.shell->dropFiles({req});
        check(rig.shell->lastMessage() && rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "demande"),
              "une page qui n'en veut pas : importFiles (la demande est notee)");
        check(rig.shell->workspace().dashboard().pendingRequests == 1, "une demande en attente");
        const std::string junk = U(fx.root / "pas-signe.txt");
        std::ofstream(junk) << "rien de signe";
        rig.shell->dropFiles({junk});
        check(rig.shell->lastMessage() && !rig.shell->lastMessage()->result.ok, "un fichier qui n'est pas signe : refuse");
        // Sur le tableau de bord : des demandes ouvrent l'assistant.
        rig.shell->navigate("tableau");
        const std::string reqC = fx.request("pc-c", "Poste essai C");
        rig.shell->dropFiles({reqC});
        rig.menus.applyPending();
        check(rig.menus.top() != rig.shell && rig.menus.top()->traits().kind != menu::MenuKind::Screen, "des .xpgreq sur le tableau de bord : l'assistant");
        rig.paint();
        rig.menus.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        rig.menus.applyPending();
        check(rig.menus.top() == rig.shell, "l'assistant ferme");
        // Nouvelle licence... : l'assistant aussi.
        // 1.41.0 : l'action principale, en haut de la fenetre (la maquette).
        check(rig.clickId("shell.nouvelle-licence"), "Nouvelle licence (en haut)");
        rig.menus.applyPending();
        check(rig.menus.top() != rig.shell, "Nouvelle licence... ouvre l'assistant");
        rig.menus.CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
        rig.menus.applyPending();
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- le tableau de bord apres une licence signee\n");
    {
        const std::string reqA = fx.request("pc-a", "Poste essai A");
        const auto signedA = fx.licence("XPG-2026-00001", "Client d'essai A", "PROFESSIONAL", {reqA});
        check(signedA.ok, "licence d'essai signee (" + signedA.text() + ")");
        const auto signedB = fx.licence("XPG-2026-00002", "Client d'essai B", "PERSONAL", {fx.request("pc-d", "Poste essai D")}, 20);
        check(signedB.ok, "seconde licence (20 jours) (" + signedB.text() + ")");
        rig.shell->reloadAll();
        rig.shell->navigate("tableau");
        const std::string t = rig.paint();
        const auto d = rig.shell->workspace().dashboard();
        check(d.clients == 2 && d.activeLicences == 2 && d.devices == 2 && d.expiringSoon == 1, "les chiffres (2 clients, 2 actives, 2 PC, 1 bientot)");
        auto* tile = find<ui::Button>(rig.root(), "tableau.tuile.actives");
        check(tile != nullptr, "la tuile Licences actives");
        check(has(t, "XPG-2026-00001") && has(t, "Client d'essai A") && has(t, "Poste essai A") && has(t, "Licence sign\xC3\xA9" "e"), "la derniere operation");
        check(has(t, "Demande re\xC3\xA7ue"), "les demandes recues dans les operations");
        check(has(t, "PROFESSIONAL") && has(t, "PERSONAL"), "la repartition par edition");
        check(!has(t, "Aucune op\xC3\xA9ration pour l'instant"), "plus de tableau vide");
        check(has(t, "sur 2 au total"), "licences actives : sur 2 au total");
        check(has(t, "Demandes en attente"), "la tuile des demandes");
        // Noter des demandes... par l'explorateur (la reponse donnee d'avance).
        const int before = rig.shell->workspace().dashboard().pendingRequests;
        ui::queueFilePick(fx.request("pc-e", "Poste essai E"));
        check(rig.clickId("tableau.noter-demandes"), "Noter des demandes...");
        check(rig.shell->lastMessage()->result.ok && has(rig.shell->lastMessage()->result.message, "not\xC3\xA9" "e"), "la demande notee (" + rig.shell->lastMessage()->result.text() + ")");
        check(rig.shell->workspace().dashboard().pendingRequests == before + 1, "une demande en attente de plus");
        // Une reponse vide (annule) : rien.
        const std::size_t logSize = rig.shell->sessionLog().size();
        ui::queueFilePick("");
        check(rig.clickId("tableau.noter-demandes") && rig.shell->sessionLog().size() == logSize, "annule : rien");
        // Les tuiles menent a leur page.
        check(rig.clickId("tableau.tuile.pc") && rig.shell->currentPageId() == "activations", "tuile PC actives -> Activations");
        rig.shell->navigate("tableau");
        check(rig.clickId("tableau.tuile.flottantes") && rig.shell->currentPageId() == "flottantes", "tuile flottantes -> sa page");
        rig.shell->navigate("tableau");
        check(rig.clickId("tableau.synchroniser") && rig.shell->currentPageId() == "service", "Service en ligne -> sa page");
        // Petite fenetre : les tuiles restent dans la page.
        rig.shell->navigate("tableau");
        rig.paint(1100.f, 700.f);
        auto* b = find<ui::Button>(rig.root(), "tableau.tuile.bientot");
        auto* page = rig.shell->currentPage();
        check(b && page && b->bounds().bottom() <= page->bounds().bottom() && b->bounds().right() <= page->bounds().right(), "a 1100 x 700, les tuiles tiennent");
    }

    // ------------------------------------------------------------------------------------------------
    std::printf("-- un dialogue sans MenuManager (une coquille seule)\n");
    {
        gui::Shell alone(U(fx.admin), [] { return g_now; });
        (void)alone.Initialize();
        alone.openDialog(gui::makeLicenceWizard(alone.host(), {}));
        check(alone.looseDialogs().size() == 1, "le dialogue est garde (pas de MenuManager)");
    }
    std::printf("-- 1.40.0 : ce que le regard en direct dit (describeChanges)\n");
    {
        online::Snapshot a, b;
        online::OnlineLicence l;
        l.licenseId = "XPG-2026-00391";
        online::OnlineActivation x, y, z;
        x.deviceKey = std::string(64, 'a');
        x.name = "PC-ATELIER-02";
        x.state = "active";
        y.deviceKey = std::string(64, 'b');
        y.name = "PC-BUREAU-03";
        y.state = "active";
        z.deviceKey = std::string(64, 'c');
        z.name = "PC-NEUF";
        z.state = "active";
        l.activations = {x, y};
        a.licences = {l};
        const auto customer = [](const std::string& id) { return id == "XPG-2026-00391" ? std::string("Atelier d'essai Meyer") : std::string(); };
        b = a;
        check(gui::Shell::describeChanges(a, b, customer).empty(), "jamais releve avant : rien a dire");
        a.at = 1000;
        b.at = 2000;
        check(gui::Shell::describeChanges(a, b, customer).empty(), "rien n'a bouge : rien");
        b.licences[0].activations[0].state = "liberee";
        b.licences[0].activations[1].state = "retiree";
        b.licences[0].activations.push_back(z);
        const auto said = gui::Shell::describeChanges(a, b, customer);
        check(said.size() == 3, "trois changements : " + std::to_string(said.size()));
        if (said.size() == 3) {
            check(said[0] == "Atelier d'essai Meyer a lib\xC3\xA9r\xC3\xA9 le PC \xC2\xAB PC-ATELIER-02 \xC2\xBB (licence XPG-2026-00391) : sa place est libre.", said[0]);
            check(said[1] == "Le PC \xC2\xAB PC-BUREAU-03 \xC2\xBB (licence XPG-2026-00391) n'est plus actif : sa place a \xC3\xA9t\xC3\xA9 reprise par un autre PC.", said[1]);
            check(said[2] == "Atelier d'essai Meyer a activ\xC3\xA9 le PC \xC2\xAB PC-NEUF \xC2\xBB (licence XPG-2026-00391).", said[2]);
        }
        const auto anon = gui::Shell::describeChanges(a, b, {});
        check(!anon.empty() && anon[0].rfind("Le client a lib", 0) == 0, "sans client connu : << Le client >>");
    }


    return finish();
}
