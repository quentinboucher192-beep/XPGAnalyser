// =============================================================================
//  license_admin/gui/Shell.hpp - 1.33.0 : LA COQUILLE DE L'APPLICATION DE
//  L'EDITEUR (XPG-License-Manager-Admin ; jamais livree a un client)
// -----------------------------------------------------------------------------
//  Un ecran (menu::WidgetMenu, "admin.shell") :
//    - a gauche, le menu (220 px) : le nom de l'application, << Editeur
//      seulement >>, puis les 13 entrees de la maquette (1.34.0 : 14, avec
//      << Demandes en ligne >>, Page.hpp ; 1.35.0 : << Service en ligne >> a la
//      place de << Synchronisation GitHub >>, << Licences flottantes >> une vraie
//      page) ; les deux de la vente en ligne (Boutique, Commandes : un paiement,
//      pas encore fait) sont grisees mais ouvrent leur page d'explication ;
//    - en haut de la page : son titre ; a droite l'etat de la cle (un clic :
//      Parametres de securite) et du service des licences (un clic : la page
//      Service en ligne) - pendant un travail lent, ce qu'il fait, avec un
//      sablier ;
//    - au centre, la page choisie (creee a la premiere visite, rafraichie a
//      chaque retour) ;
//    - en bas, le bandeau du dernier resultat (vert : fait ; rouge : refuse),
//      ses details a la demande, et le journal de la seance (tous les resultats).
//  La coquille est le ShellHost des pages : elle tient le Workspace, le travail
//  lent (Background, suivi a chaque image : jamais d'attente sur le fil de
//  l'interface), les dialogues (poses sur le MenuManager), l'explorateur de
//  fichiers du systeme (ui::pickFile) et l'ouverture d'un dossier.
//  Sans SDL : les essais la dessinent a blanc (tests/admin_gui_shell_test.cpp).
// =============================================================================
#pragma once

#include "Page.hpp"
#include "Workspace.hpp"

#include "../../menu/IMenu.hpp"
#include "../../ui/Icons.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui {

class Shell : public menu::WidgetMenu {
public:
    // Une entree du menu de gauche (l'ordre de la maquette).
    struct NavEntry {
        const char* id;       // l'identifiant de la page ("licences")
        const char* label;    // UTF-8 ("Licences")
        ui::Icon    icon;
        bool        online;   // la vente en ligne (pas encore) : grisee, la page explique
    };
    [[nodiscard]] static const std::vector<NavEntry>& entries();
    [[nodiscard]] static const NavEntry* entry(const std::string& pageId);

    // Une ligne du journal de la seance : un resultat (report) ou un message (flash).
    struct LogLine {
        Seconds  at{0};
        Tone     tone{Tone::Info};
        OpResult result;
    };

    // `folder` vide : le dossier de la cle (defaultKeyFolder). `clock` (les essais) : l'heure du Workspace.
    explicit Shell(std::string folder = {}, std::function<Seconds()> clock = {});
    ~Shell() override;

    [[nodiscard]] std::string title() const override { return kAppName; }

    // ---- ce que les pages recoivent ----
    [[nodiscard]] ShellHost host();
    [[nodiscard]] Workspace& workspace() noexcept { return *ws_; }

    // ---- le ShellHost, en clair ----
    void report(const OpResult& r);
    void flash(const std::string& text, Tone tone);
    bool runSlow(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);
    [[nodiscard]] bool busy() const noexcept { return bg_.running(); }
    [[nodiscard]] std::string busyLabel() const;
    void navigate(const std::string& pageId);
    void openDialog(menu::MenuPtr dialog);
    void pickFile(const ui::FilePick& pick, std::function<void(std::string)> done);
    void openFolder(const std::string& folder);

    // Des fichiers glisses sur la fenetre : a la page montree ; elle n'en veut pas -> ranges dans l'archive
    // (importFiles), sauf pendant un travail lent.
    void dropFiles(const std::vector<std::string>& paths);
    // Tout relire (F5) : le Workspace, puis chaque page.
    void reloadAll();

    // Le travail lent fini : son `done`, puis report, sur ce fil. Appele a chaque image (Update, et la boucle
    // de la fenetre : un dialogue ouvert par-dessus n'arrete pas le suivi). Vrai : un resultat a ete rendu.
    bool poll();
    // Les essais : attendre la fin du travail lent et rendre son resultat (vrai : rendu).
    bool finishSlow();

    // 1.36.0 : LA RELEVE AUTOMATIQUE DES DEMANDES EN LIGNE - au lancement, puis toutes les 2 minutes tant que
    // l'application est ouverte (le service regle, la cle la, aucun travail en cours, aucun dialogue ouvert) ; sans bruit :
    // rien au journal, sauf une NOUVELLE demande (un avis : le client, le PC -> Demandes en ligne) ou la premiere
    // panne (une seule fois, jusqu'a la prochaine releve reussie). Vrai : une releve est partie (les essais l'appellent).
    bool autoFetchRequests();
    // Les demandes en attente de ta decision (le menu de gauche les compte : Demandes en ligne).
    [[nodiscard]] int pendingRequests() const;
    static constexpr double kAutoFetchFirst = 4.0, kAutoFetchPeriod = 120.0;   // secondes

    // ---- ce qui se lit (les essais, la fenetre) ----
    [[nodiscard]] const std::string& currentPageId() const noexcept { return current_; }
    [[nodiscard]] Page* page(const std::string& pageId) const;   // nul : pas encore ouverte
    [[nodiscard]] Page* currentPage() const { return page(current_); }
    [[nodiscard]] const std::vector<LogLine>& sessionLog() const noexcept { return log_; }
    [[nodiscard]] const LogLine* lastMessage() const noexcept { return log_.empty() ? nullptr : &log_.back(); }
    [[nodiscard]] std::string keyStatusText() const;
    [[nodiscard]] Tone keyStatusTone() const;
    [[nodiscard]] std::string syncStatusText() const;
    [[nodiscard]] std::string pageTitle() const;
    [[nodiscard]] bool detailsOpen() const noexcept { return detailsOpen_; }
    [[nodiscard]] bool logOpen() const noexcept { return logOpen_; }
    void setDetailsOpen(bool open);
    void setLogOpen(bool open);
    // Les dialogues ouverts sans MenuManager (les essais qui n'en posent pas) ; sinon vide.
    [[nodiscard]] std::vector<menu::MenuPtr>& looseDialogs() noexcept { return loose_; }

    void            Update(const menu::FrameContext& fc) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

protected:
    core::Status buildUi() override;
    void         onEnter() override { attached_ = true; }

private:
    Page* ensurePage(const std::string& pageId);
    void  showCurrent();
    void  refreshPages();
    void  pushLog(Tone tone, OpResult r);

    std::unique_ptr<Workspace> ws_;
    std::string                current_{"tableau"};
    std::map<std::string, Page*> pages_;         // dans l'arbre (stack_) ; crees a la premiere visite
    std::vector<LogLine>       log_;
    bool                       detailsOpen_{false}, logOpen_{false};
    bool                       attached_{false};
    std::vector<menu::MenuPtr> loose_;
    ui::Widget*                stack_{nullptr};   // la pile des pages (l'arbre la possede)
    ui::Widget*                view_{nullptr};    // la racine
    std::function<void(const OpResult&)> pendingDone_;
    bool                       quietSlow_{false};        // 1.36.0 : le travail en cours est la releve automatique
    double                     nextAutoFetch_{-1};       // 1.36.0 : l'heure (steady, secondes) de la prochaine releve
    bool                       autoProblemSaid_{false};  // 1.36.0 : la panne de la releve deja dite
    // DERNIER : detruit le premier (le fil du travail lent se termine avant que le Workspace s'en aille).
    Background                 bg_;
};

} // namespace license_admin::gui
