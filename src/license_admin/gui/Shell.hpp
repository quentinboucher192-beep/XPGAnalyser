// =============================================================================
//  license_admin/gui/Shell.hpp - 1.33.0 : LA COQUILLE DE L'APPLICATION DE
//  L'EDITEUR (XPG-License-Manager-Admin ; jamais livree a un client)
// -----------------------------------------------------------------------------
//  Un ecran (menu::WidgetMenu, "admin.shell") :
//    - a gauche, le menu (220 px) : le nom de l'application, << Editeur
//      seulement >>, puis les 13 entrees de la maquette (1.34.0 : 14, avec
//      << Demandes en ligne >>, Page.hpp ; 1.35.0 : << Service en ligne >> a la
//      place de << Synchronisation GitHub >>, << Licences flottantes >> une vraie
//      page) ; 1.46.0 : la vente en ligne (Boutique, Commandes : Stripe,
//      PageShop.cpp) est faite - plus aucune entree grisee ;
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
#include <optional>
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
    // 1.44.0 : une entree du menu, ou une SOUS-PAGE (<< droits >> : Administrateurs > Droits) - pas dans le menu, mais on
    // y va (navigate, --page) ; le menu montre alors sa page parente choisie.
    [[nodiscard]] static const NavEntry* entry(const std::string& pageId);
    [[nodiscard]] static const std::vector<NavEntry>& subPages();
    [[nodiscard]] static std::string parentOf(const std::string& pageId);   // "" : une entree du menu
    // 1.44.0 : les pages de l'admin+ seul (le journal des admins, les droits) : un autre role ne les voit pas dans le menu.
    // 1.52.0 : XPG Admin (pageAllowed) - sans derogation, la gestion seulement (ni Parametres de securite, ni Service en
    // ligne, ni Administrateurs, ni ces trois) ; une derogation de l'admin+ qui court : le menu de l'admin+, tout entier.
    [[nodiscard]] static bool plusOnly(const std::string& pageId);
    [[nodiscard]] bool pageAllowed(const std::string& pageId) const;

    // 1.44.0 : UNE ALERTE EN DIRECT (planche 04, en bas a gauche) : une ligne du journal des admins dont la criticite dit
    // << Me prevenir tout de suite >>, relevee par le regard ; montree meme quand la page n'est pas ouverte.
    struct Toast {
        std::uint64_t number{0};   // la ligne du journal
        std::string   title;       // << Critique : Hugo a revoque XPG-2026-00377 >>
        std::string   line;        // << a l'instant - PORTABLE-HUGO - Brasserie du Nord >>
        int           level{0};    // licensing::admins::Level
        double        shownAt{0};  // steady, secondes
        bool          derogation{false};   // 1.52.0 : une derogation recue (XPG Admin) - << Voir >> ouvre le dialogue du code
    };
    [[nodiscard]] const std::vector<Toast>& toasts() const noexcept { return toasts_; }
    void dismissToasts();
    void openToast();   // << Voir >> : le journal, la ligne depliee (1.52.0 : une derogation recue - le dialogue du code)
    // 1.52.0 (docs/ADMINS.md, par. 11) : LA DEROGATION PAR CODE - XPG Admin : le regard voit une derogation EN ATTENTE pour
    // sa cle (une nouvelle) -> l'alerte (<< L'admin+ t'a envoye une derogation (4 h) : tape le code qu'il voit sur son PC. >>),
    // le dialogue du code s'ouvre seul une fois (sous un autre dialogue : apres), le bouton de la barre des taches clignote
    // (Windows), la pastille ambre << Derogation recue : taper le code >> en haut ; activee : << Derogation : 3 h 52 >>.
    // L'Admin+ : le regard la voit activee -> << Lea Essai a tape le code : sa derogation court jusqu'a 19:42. >>.
    void openDerogationCode();
    // Le texte de la pastille (vide : pas de pastille) ; `pending` : en attente du code (ambre).
    [[nodiscard]] std::string derogationChipText(bool* pending = nullptr) const;
    // La fenetre du programme (AdminGuiMain) : attirer l'attention (le bouton de la barre des taches clignote ; ailleurs rien).
    void setAttention(std::function<void()> f) { attention_ = std::move(f); }
    // Les alertes pas encore vues (le nombre du menu, << Journal des admins >>) ; le journal ouvert les remet a zero.
    [[nodiscard]] int unseenAlerts() const noexcept { return unseenAlerts_; }
    // Les essais : faire comme le regard quand il a releve de nouvelles lignes (les alertes -> les toasts).
    void takeAlerts();
    static constexpr double kToastSeconds = 30.0;

    // Une ligne du journal de la seance : un resultat (report) ou un message (flash).
    struct LogLine {
        Seconds  at{0};
        Tone     tone{Tone::Info};
        OpResult result;
    };

    // `folder` vide : le dossier de la cle (defaultKeyFolder). `clock` (les essais) : l'heure du Workspace.
    explicit Shell(std::string folder = {}, std::function<Seconds()> clock = {});
    ~Shell() override;

    [[nodiscard]] std::string title() const override { return appName(); }

    // ---- ce que les pages recoivent ----
    [[nodiscard]] ShellHost host();
    [[nodiscard]] Workspace& workspace() noexcept { return *ws_; }
    [[nodiscard]] const Workspace& workspace() const noexcept { return *ws_; }

    // ---- le ShellHost, en clair ----
    void report(const OpResult& r);
    void flash(const std::string& text, Tone tone);
    bool runSlow(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);
    // 1.44.0 : un travail lent sans bruit (ShellHost::runQuiet) : rien au journal de la seance, pas << occupe >>.
    bool runQuiet(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);
    // 1.40.0 : occupe POUR TOI - un travail que tu as lance, ou qui attend. Le regard en direct (un appel court, sans
    // bruit) n'occupe pas : un travail lance pendant qu'il tourne attend sa fin (une file d'une place) et part seul.
    [[nodiscard]] bool busy() const noexcept { return (bg_.running() && !quietSlow_) || queued_.has_value(); }
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
    // rien au journal, sauf une NOUVELLE demande (un avis : le client, le PC -> Commandes et demandes) ou la premiere
    // panne (une seule fois, jusqu'a la prochaine releve reussie). Vrai : une releve est partie (les essais l'appellent).
    bool autoFetchRequests();
    // Les demandes en attente de ta decision (le menu de gauche les compte : Commandes et demandes, 1.48.1).
    [[nodiscard]] int pendingRequests() const;
    static constexpr double kAutoFetchFirst = 4.0, kAutoFetchPeriod = 120.0;   // secondes

    // 1.40.0 : LE REGARD EN DIRECT - tant que l'application est ouverte (le service regle, la cle la, aucun dialogue),
    // toutes les 15 secondes : GET /admin/etat (des compteurs) ; un PC active ou libere par son client, une demande
    // deposee : les licences (ou les demandes) sont relues seules, les pages suivent, et un avis dit ce qui a change
    // (<< Atelier Meyer a libere le PC ... >>). Un service d'avant la 1.40.0 (sans /admin/etat) : les licences sont
    // relues toutes les 30 secondes. Sans bruit : la premiere panne est dite une fois. Vrai : un regard est parti.
    // 1.44.0 : pour l'admin+ ET pour un admin (Workspace::signer, plus seulement la cle de l'editeur) ; apres l'etat, la
    // liste des admins et le journal (Workspace::refreshAdminState) : un droit change, une ligne << Me prevenir >> (un
    // toast) ; EN COURS D'AJOUT (pas encore d'acces a /admin/etat) : l'inscription par code, puis, signee, l'archive recue.
    bool autoWatch();
    // Relire les licences en ligne sans bruit, et dire ce qui a change depuis le dernier releve (les essais l'appellent).
    bool autoRefreshLicences();
    // Ce qui a change entre deux releves, en phrases (un PC active, libere par son client, retire) ; vide : rien, ou
    // pas de releve d'avant. `customerOf` : le client d'une licence (l'archive).
    [[nodiscard]] static std::vector<std::string> describeChanges(const online::Snapshot& before, const online::Snapshot& after,
                                                                  const std::function<std::string(const std::string&)>& customerOf);
    // Le regard marche (le haut de la fenetre le dit : << en direct >>).
    [[nodiscard]] bool live() const;
    // 1.41.0 : depuis quand le service a repondu au regard (secondes ; -1 : jamais) - la carte du menu le dit.
    [[nodiscard]] double secondsSinceLive() const;
    static constexpr double kWatchFirst = 2.0, kWatchPeriod = 15.0, kWatchFallback = 30.0, kFetchWhenWatched = 600.0;

    // 1.45.0 : AU LANCEMENT, la version du service (/v1/etat, sans bruit) : plus ancienne que ce qu'il faut a l'Admin (ou
    // que son kit) -> un avis, << Service : ... - a mettre a jour >> en haut, le bandeau du tableau de bord. Une fois, une
    // seconde apres le lancement ; pas si l'etat est deja lu. Vrai : la lecture est partie (les essais l'appellent).
    bool checkServiceVersion();
    static constexpr double kVersionCheckFirst = 1.0;

    // 1.46.0 : LA BOUTIQUE (ShellShop.cpp ; docs/BOUTIQUE.md, par. 5) - le regard voit le compteur des commandes bouger
    // (GET /admin/etat : commandes, a_livrer) : les commandes et le reglage sont relus sans bruit (autoOrders) ; une
    // commande payee, << Livrer seul >> coche et cet Admin qui peut signer (l'admin+, ou un admin avec << demandes >> et
    // << licence_creer >>) : la livraison part (autoDeliver : un travail lent, dit dans le journal de la seance), un avis
    // par commande livree ou a revoir. Sans livraison automatique : un avis (<< Commandes > Delivrer maintenant >>). Une
    // livraison qui n'a pas pu se faire (le reseau) est reprise toutes les 5 minutes. Vrai : un travail est parti.
    bool autoOrders();
    bool autoDeliver();
    // Les commandes payees a livrer (le menu de gauche les compte : Commandes ; le tableau de bord les dit).
    [[nodiscard]] int ordersToDeliver() const noexcept { return toDeliver_ > 0 ? toDeliver_ : 0; }
    static constexpr double kDeliveryRetry = 300.0;

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
    // 1.40.0 : le regard en direct
    struct Queued {
        std::string                          label;
        std::function<OpResult()>            work;
        std::function<void(const OpResult&)> done;
    };
    std::optional<Queued>      queued_;                  // un travail lance pendant le regard : il part a sa fin
    void                       startQueued();
    double                     nextWatch_{-1};
    std::int64_t               knownActivity_{-1}, knownRequests_{-1};
    bool                       watchSupported_{true};    // faux : un service d'avant la 1.40.0
    bool                       wantLicences_{false}, wantRequests_{false};   // a relire des que possible
    double                     lastLive_{-1};            // la derniere fois que le service a repondu au regard
    bool                       autoProblemSaid_{false};  // 1.36.0 : la panne de la releve deja dite
    // 1.44.0 : plusieurs admins
    bool                       watchPending();           // en cours d'ajout : l'inscription par code, toutes les 15 s
    bool                       wantArchive_{false};      // admis : recevoir l'archive des que possible
    std::string                pendingState_;            // le dernier etat dit de l'inscription ("aucune"...)
    // 1.51.0 : XPG Admin, premier lancement - le code de l'installateur (invitation.txt) : devenir admin tout seul
    bool                       autoJoin();
    bool                       joinTried_{false};
    std::vector<Toast>         toasts_;
    int                        unseenAlerts_{0};
    double                     nextBackupCheck_{-1};     // la sauvegarde de secours du jour (l'admin+)
    bool                       versionChecked_{false};   // 1.45.0 : la version du service, lue au lancement
    double                     versionCheckAt_{-1};
    // 1.46.0 : la boutique (ShellShop.cpp)
    std::int64_t               knownOrders_{-1};         // le compteur des commandes vu par le regard
    int                        toDeliver_{0};            // les commandes payees a livrer (le regard)
    bool                       wantOrders_{false}, wantDelivery_{false};
    double                     nextDelivery_{-1};        // la prochaine reprise d'une livraison (steady)
    std::vector<int>           seenPaid_;                // les commandes payees deja annoncees
    bool                       autoBackup();
    // 1.52.0 : XPG Admin - la derogation de l'admin+ ouvre tout ; elle finit : retour a la gestion (Shell.cpp, ClassicView.hpp)
    void                       classicGate();
    bool                       derogationOpen_{false};
    // 1.52.0 (agent D) : la derogation recue (XPG Admin) - deja dite (son identifiant), le dialogue du code a ouvrir
    void                       derogationTick();
    std::string                derogationNoticed_;
    bool                       wantCodeDialog_{false};
    std::function<void()>      attention_;
    // DERNIER : detruit le premier (le fil du travail lent se termine avant que le Workspace s'en aille).
    Background                 bg_;
};

} // namespace license_admin::gui
