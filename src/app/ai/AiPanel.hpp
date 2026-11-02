// =============================================================================
//  app/ai/AiPanel.hpp - le panneau "Assistant IA" (IHM Studio, editeur de vues)
// -----------------------------------------------------------------------------
//  UN VOLET DE LA FENETRE, A DROITE DE L'EDITEUR (l'audit, section 3.1) : il
//  pousse l'editeur au lieu de le cacher, se tire, se replie et se retient comme
//  les autres panneaux. De haut en bas :
//
//    l'en-tete        l'icone, "Assistant IA", l'etat de connexion, Nouvelle
//                     conversation, Parametres, Replier ; le mode Previsualisation |
//                     Direct ; la vue visee ;
//    les conversations la liste (repliable) : choisir, renommer (double-clic, F2,
//                     clic droit), supprimer (Suppr, clic droit : une confirmation) ;
//    la discussion    les messages, l'horodatage, la mise en forme (gras, listes,
//                     `code`, titres), la carte des operations proposees (son apercu
//                     dessine par les painters, Previsualiser / Appliquer / Refuser),
//                     l'indicateur de generation ; elle defile seule ;
//    les pieces       celles du prochain message (type, taille, traitement, retirer) ;
//    la saisie        toujours visible : Entree envoie, Maj+Entree passe a la ligne
//                     (Ctrl+Entree envoie aussi), Joindre..., Envoyer (Arreter pendant
//                     une generation) ; un fichier lache sur le panneau s'y joint.
//
//  Le panneau ne fait que montrer et demander : la seance (ai::Assistant) garde
//  les conversations et applique ; l'hote (l'ecran) ouvre les fenetres.
//
//  1.21.2 : DEUX ONGLETS sous l'en-tete - << Conversation >> (tout ce qui est
//  dit plus haut) et << Notifications >> (sa pastille : le nombre de non lues,
//  rouge s'il y a une erreur non lue). Le second montre, a la place de la
//  conversation, le journal de l'Assistant (AiNotificationsView,
//  AiNotifications.hpp) ; un autre clic, ou l'onglet Conversation, y revient.
//  Le bouton d'une notification mene a la chose (followNotification).
//
//  1.22.0 : LES MISSIONS IA PERSISTANTES - un troisieme onglet << Missions IA >>
//  (AiMissionsView : consulter, chercher, creer, reprendre, mettre en pause,
//  annuler, archiver) ; au-dessus de la conversation, la bande << Mission IA >>
//  de la mission de la conversation ouverte (AiMissionStrip) ; une demande
//  complexe envoyee demande d'abord << Creer une mission IA ? >> (AiMissionAsk,
//  au-dessus de la saisie). Le tableau de bord d'une mission s'ouvre au centre
//  (UiHost::openMission, AiMissionBoard) ; la notification << mission:<id> >> y
//  mene. Les gestes sur une mission passent par missionAct / missionProposal.
// =============================================================================
#pragma once

#include "AiAssistant.hpp"
#include "AiFixes.hpp"   // 1.22.1 : le clic droit sur une erreur de la carte
#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <atomic>
#include <thread>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiChatView;
class AiPlanStrip;
class AiContextPane;   // 1.21.0
class AiNotificationsView;   // 1.21.2
class AiMissionsView;        // 1.22.0 : l'onglet Missions IA
class AiMissionStrip;        // 1.22.0 : la mission active, au-dessus de la conversation
class AiMissionAsk;          // 1.22.0 : << Creer une mission IA ? >>, au-dessus de la saisie
class AiPreviewDialog;       // 1.22.5 : la previsualisation ouverte (l'avis du logiciel lui est passe)

// Le champ de la demande : UN CHAMP DE MESSAGE, pas un editeur de code (les captures 1.14.2 :
// la gouttiere, la bande de la ligne courante, la marque "modifie" sur l'indication, le texte
// qui defilait de cote). Le texte passe a la ligne au mot ; l'indication (le champ vide) se lit
// en entier, rien ne la recouvre ; le champ grandit de 1 a kMaxRows lignes, puis defile.
// Entree envoie, Maj+Entree passe a la ligne (Ctrl+Entree envoie aussi). Le reste : le
// clavier d'un champ (fleches, Debut, Fin, Ctrl+fleches, Ctrl+A/C/X/V, Ctrl+Z), la souris
// (clic, glisser, double-clic : le mot), la molette.
class AiPromptEdit final : public ui::Widget {
public:
    static constexpr std::size_t kMaxRows = 5;
    explicit AiPromptEdit(std::string id);
    const core::SignalPtr<> submitted = core::Signal<>::create();

    void setText(std::string t);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    void insertText(std::string_view utf8);           // a la place de la selection
    void insertNewline() { insertText("\n"); }
    void setReadOnly(bool ro) { readOnly_ = ro; invalidate(); }
    [[nodiscard]] bool readOnly() const noexcept { return readOnly_; }
    void setPlaceholder(std::string p) { placeholder_ = std::move(p); invalidate(); }
    [[nodiscard]] const std::string& placeholder() const noexcept { return placeholder_; }
    // Les lignes montrees a cette largeur (le cadre) : celles du texte, ou de l'indication
    // quand il est vide (deux au plus) ; et la hauteur voulue (1 a kMaxRows lignes).
    [[nodiscard]] std::size_t rowsFor(float width) const;
    [[nodiscard]] float       heightFor(float width) const;
    [[nodiscard]] std::size_t lineCount() const { return rowsFor(bounds().w); }
    [[nodiscard]] std::size_t caretOffset() const noexcept { return caret_; }
    [[nodiscard]] float       scrollOffset() const noexcept { return scroll_; }
    [[nodiscard]] std::optional<ui::cursor::Shape> cursorAt(gfx::Point local) const override;

    // Une ligne mise en page : [begin, end) du texte, `next` le debut de la suivante
    // (end + 1 apres un retour a la ligne ; end quand le mot suivant ne tenait pas).
    struct Row { std::size_t begin{0}, end{0}, next{0}; };
    [[nodiscard]] static std::vector<Row> wrap(std::string_view text, float width);

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;
    void onFocusChanged(bool focused) override;

private:
    [[nodiscard]] gfx::Rect textArea() const;
    [[nodiscard]] float     wrapWidth() const;
    const std::vector<Row>& rows() const;                       // a la largeur du cadre
    [[nodiscard]] std::size_t rowOf(std::size_t offset) const;
    [[nodiscard]] std::size_t rowLimit(std::size_t row) const;  // le dernier curseur de la ligne
    [[nodiscard]] std::size_t offsetAt(gfx::Point p) const;
    [[nodiscard]] std::size_t offsetInRow(std::size_t row, float x) const;
    void replaceSelection(std::string_view with);
    void moveTo(std::size_t offset, bool extend);
    void ensureCaretVisible();
    void changed();                                             // le texte a change

    std::string        text_, placeholder_, undoText_;
    std::size_t        caret_{0}, anchor_{0};
    float              goalX_{-1.f};          // la colonne visee par Haut et Bas
    float              scroll_{0.f};
    bool               readOnly_{false};
    bool               selecting_{false};
    double             blinkFrom_{0.0}, lastTime_{0.0};
    ui::EdgeScrollBar  bar_;
    mutable std::vector<Row> rows_;
    mutable float            rowsW_{-1.f};
    mutable std::uint64_t    rowsRev_{~0ull};
    std::uint64_t            rev_{0};
};

// 1.14.2 : LA LARGEUR PAR DEFAUT DU VOLET, proportionnelle a la fenetre : 22 % de sa largeur,
// entre 280 et 460 px (1280 x 800 : 282 px, l'editeur garde sa place ; 1920 : 422 px). L'ecran la
// pose tant que l'utilisateur n'a pas tire le volet (AiWorkspace.cpp) ; tiree, elle est la sienne.
[[nodiscard]] float defaultPanelWidth(float windowWidth) noexcept;

// 1.22.8 : DES ERREURS QUI NE DIFFERENT QUE PAR LE PREMIER NOM CITE (26 variables, la meme unite inconnue) en UNE ligne :
// << Variable << A >>, << B >>, << C >> et 23 autres (26 erreurs) : unite : unite inconnue << mmCE >> ... >> - trois et plus ; les
// autres telles quelles, dans l'ordre de leur premiere apparition (la confirmation << Appliquer malgre >>).
[[nodiscard]] std::vector<std::string> groupedErrors(const std::vector<std::string>& lines);

class AiPanel final : public ui::Widget {
public:
    // Ce que l'ecran fait pour le panneau (un essai, le sien).
    struct UiHost {
        std::function<void(menu::MenuPtr, std::function<void(const menu::DialogResult&)>)> showDialog;
        std::function<void(const std::string&)> status;          // la barre d'etat
        std::function<void()>                   close;           // replier le panneau
        std::function<bool()>                   memoryAvailable; // la commande ia.memoire existe-t-elle ?
        std::function<void()>                   openMemory;      // Memoire et apprentissage...
        // 1.16.1 : une reponse vient de finir (l'option << M'avertir des reponses >> cochee) : l'ecran avertit si
        // l'utilisateur ne regarde pas (la cloche, la barre des taches). conversation : son identifiant ; excerpt : le
        // debut de la reponse ; error : elle a fini en erreur.
        std::function<void(const std::string& conversation, const std::string& title, const std::string& excerpt, bool error)> replied;
        // 1.18.0 : << Detecter ce qui repond... >> (une carte appliquee qui a cree un equipement avec la detection
        // proposee) : l'ecran ouvre Communication > Scanner IP, la plage remplie - SANS lancer : c'est l'utilisateur qui
        // clique Demarrer. Faux et `why` : l'onglet n'a pas pu s'ouvrir.
        std::function<bool(const std::string& equipment, const std::string& range, std::string* why)> openScan;
        // ---- 1.21.2 : l'onglet Notifications (a la fin ; nuls : les essais) ----
        // La fenetre a le clavier (nul : oui) - une reponse qui arrive fenetre derriere une autre est notee.
        std::function<bool()> windowFocused;
        // Memoire et apprentissage > Souvenirs, filtre sur ce statut ("provisoire" : a valider, "conflits") ; nul : openMemory.
        std::function<void(const std::string& status)> openSouvenirs;
        // Le compte rendu du dernier scan (Communication > Scanner IP) ; faux : pas ouvert.
        std::function<bool()> showScanReport;
        // IHM > Ressources > Incidents, ce dossier d'incident choisi ; faux : introuvable.
        std::function<bool(const std::string& folderName)> openIncident;
        // ---- 1.22.0 : les missions IA (a la fin ; nuls : les essais) ----
        // Le tableau de bord d'une mission, AU CENTRE (un onglet par mission) ; `mode` 0 : le tableau de bord, 1 : l'ecran
        // de reprise, 2 : l'historique. Faux : il n'a pas pu s'ouvrir.
        std::function<bool(const std::string& missionId, int mode)> openMission;
        // Montrer le panneau (replie : le deplier) - le tableau de bord y mene (Verifier une proposition, la conversation).
        std::function<void()> showPanel;
    };

    AiPanel(std::string id, Assistant::Host core, UiHost ui);
    ~AiPanel() override;

    [[nodiscard]] Assistant&       assistant() noexcept { return *assistant_; }
    [[nodiscard]] const Assistant& assistant() const noexcept { return *assistant_; }

    // A chaque image (l'ecran) : la generation, les pieces jointes, l'apercu de la carte.
    void tick(double now);
    // Ce que la seance a change, remis a l'ecran (sans faire avancer son horloge).
    void sync();
    // Relit les reglages et les conversations, une fois (la premiere fois que le panneau se montre).
    void ensureLoaded();
    [[nodiscard]] bool loaded() const noexcept { return loaded_; }

    // ---- les gestes (les boutons, le clavier, les essais, les seances de script) ----------
    void newConversation();
    bool selectConversation(int index);
    void startRename(int index);                   // le champ sur la ligne
    bool commitRename(const std::string& title);   // Entree
    void cancelRename();
    [[nodiscard]] bool renaming() const noexcept { return renameIndex_ >= 0; }
    void askDelete(int index);                     // la confirmation, puis supprimer
    bool send();                                   // le texte de la saisie
    bool sendText(const std::string& text);        // une suggestion, une seance
    void stop();
    void askAttach();                              // le selecteur de fichiers du logiciel
    bool attachFile(const std::string& path);      // un fichier choisi ou lache
    void removePending(std::size_t index);
    void openPreview(int message);                 // la fenetre de previsualisation
    bool applyProposal(int message);               // la confirmation d'une suppression multiple d'abord
    bool reapplyProposal(int message);             // une proposition annulee (Ctrl+Z) : de nouveau
    bool refuseProposal(int message);
    bool unrefuseProposal(int message);            // 1.16.1 : revenir sur un refus
    bool detectFromProposal(int message);          // 1.18.0 : ouvrir le scanner (la plage proposee) - sans le lancer
    [[nodiscard]] bool canDetect(int message) const;   // la carte offre-t-elle << Detecter ce qui repond... >> ?
    // 1.16.1 : REFUSER AVEC UNE QUESTION (la Previsualisation) - << Refuser cette proposition ? >> [Refuser] [Garder] ;
    // Echap ou Garder : rien ne change.
    void askRefuse(int message);
    void editRequest(int message);                 // Modifier la demande : le texte remis dans la saisie
    // 1.22.4 : LE CHOIX sur la carte d'une proposition qui tourne en rond (Assistant::chooseLoop) : Corriger a la main ouvre
    // la previsualisation ; Modifier la demande remet la demande (une mission : le debut de la consigne) dans la saisie ;
    // Forcer la fin de la mission pose d'abord la question (missionAct). Faux : refuse, et dit pourquoi.
    bool chooseLoop(int message, Assistant::LoopChoice choice);
    // 1.22.1 : LE CLIC DROIT SUR UNE ERREUR (AiFixes.hpp) - << Demander a l'IA de corriger >> : le texte prepare dans la
    // saisie (RIEN ne part seul : l'utilisateur relit, puis Entree) ; << Choisir une variable... >> depuis la carte :
    // la previsualisation, son choix du haut sur cette variable ; ce que la barre d'etat dit (la fenetre d'apercu).
    void askFix(const std::string& text);
    void openPreviewFor(int message, const std::string& variable);
    void notice(const std::string& text);
    // 1.22.8 : << CREER L'UNITE << X >>... >> (une unite inconnue, la carte ou la previsualisation) - la petite fenetre, PRE-REMPLIE
    // quand le logiciel reconnait le symbole (AiFixes : unitForm) ; Ajouter : l'unite va dans la PROPOSITION (Assistant::addUnit),
    // jamais dans le projet. `done` (non nul) : apres la fenetre (vrai : ajoutee).
    void askUnit(int message, const std::string& symbol, std::function<void(bool)> done = {});
    void openSettings();
    // 1.15.0 : Reessayer (le dernier message est une erreur ou un arret d'un vrai fournisseur).
    bool retry();
    void setDirect(bool direct);
    void setConversationsOpen(bool open);
    [[nodiscard]] bool conversationsOpen() const noexcept { return listOpen_; }
    bool clickSuggestion(std::size_t index);

    // ---- ce qu'on lit (essais, scripts) --------------------------------------------------
    [[nodiscard]] AiPromptEdit&  prompt() noexcept { return *prompt_; }
    [[nodiscard]] AiChatView&    chat() noexcept { return *chat_; }
    [[nodiscard]] AiPlanStrip&   planStrip() noexcept { return *strip_; }   // 1.20.2 : le plan en cours, en haut
    [[nodiscard]] ui::InputText& renameField() noexcept { return *rename_; }
    [[nodiscard]] ui::PopupMenu* menu() const noexcept { return menu_; }
    [[nodiscard]] gfx::Rect      rowRect(int index) const;     // une conversation, apres un dessin (vide : cachee)
    [[nodiscard]] gfx::Rect      promptRect() const;
    // 1.15.0 : la ligne "ce qui part" sous la saisie (vide : pas montree), et l'etat de connexion.
    [[nodiscard]] gfx::Rect      outgoingRect() const noexcept { return outgoing_; }
    [[nodiscard]] gfx::Rect      statusRect() const noexcept { return statusLine_; }
    // 1.16.0 : la bande de la limite et de la consommation, sous l'etat de connexion (toujours a jour :
    // le compte a rebours avance sans envoi), ses lignes montrees et ce qu'elle dit (essais).
    [[nodiscard]] gfx::Rect      limitRect() const noexcept { return limitLine_; }
    [[nodiscard]] const std::vector<std::string>& limitLines() const noexcept { return bandLines_; }
    [[nodiscard]] const UsageBand& usageBand() const noexcept { return band_; }
    [[nodiscard]] gfx::Rect      chatRect() const;
    [[nodiscard]] std::string    lastStatus() const { return lastStatus_; }
    // Le menu du clic droit d'une conversation (Renommer, Supprimer...).
    void openRowMenu(int index, gfx::Point at);
    enum MenuId : int { kMenuOpen = 1, kMenuRename, kMenuDelete, kMenuNew };

    // ---- 1.21.2 : L'ONGLET NOTIFICATIONS ----------------------------------------------------
    void showNotifications(bool on);               // l'onglet Notifications (vrai) ou Conversation (faux)
    [[nodiscard]] bool notificationsShown() const noexcept { return notifsShown_; }
    [[nodiscard]] AiNotificationsView& notifications() noexcept { return *notifView_; }
    [[nodiscard]] gfx::Rect conversationTabRect() const noexcept { return tabConv_; }
    [[nodiscard]] gfx::Rect notificationsTabRect() const noexcept { return tabNotifs_; }
    // La pastille de l'onglet : les non lues (0 : pas de pastille), rouge s'il y a une erreur non lue (sinon l'accent).
    [[nodiscard]] std::size_t badgeCount() const noexcept { return badge_; }
    [[nodiscard]] bool        badgeAlert() const noexcept { return badgeError_; }
    // Le bouton d'une notification (AiNotifications.hpp, ses liens) : la conversation et le message, la carte, la piece
    // jointe, la fenetre << Modele de fichier >>, les Souvenirs, le compte rendu du scan, l'incident. Faux : introuvable
    // (la barre d'etat le dit).
    bool followNotification(const std::string& action);

    // ---- 1.22.0 : LES MISSIONS IA ---------------------------------------------------------
    // L'onglet Missions IA (vrai) ; showConversation : l'onglet Conversation (ni Notifications, ni Missions IA).
    void showMissions(bool on);
    void showConversation();
    [[nodiscard]] bool              missionsShown() const noexcept { return missionsShown_; }
    [[nodiscard]] AiMissionsView&   missions() noexcept { return *missionsView_; }
    [[nodiscard]] AiMissionStrip&   missionStrip() noexcept { return *missionStrip_; }
    [[nodiscard]] AiMissionAsk&     missionAsk() noexcept { return *missionAsk_; }
    [[nodiscard]] gfx::Rect         missionsTabRect() const noexcept { return tabMissions_; }
    [[nodiscard]] std::size_t       missionsBadge() const noexcept { return missionsBadge_; }
    // << Creer une mission IA ? >> : une demande complexe envoyee (Assistant::suggestsMission) n'est pas partie - la carte
    // de choix au-dessus de la saisie. Creer la mission (vrai) ou Continuer en conversation (l'envoi normal) ;
    // dismissMissionAsk (la croix, Echap) : rien ne part, le texte reste dans la saisie.
    [[nodiscard]] bool               missionAskShown() const noexcept { return !askText_.empty(); }
    [[nodiscard]] const std::string& missionAskText() const noexcept { return askText_; }
    bool                             chooseMission(bool create);
    void                             dismissMissionAsk();
    // Creer une mission (l'onglet Missions IA, la carte de choix) : l'IA analyse le projet et propose un plan.
    bool createMission(const std::string& request);
    // 1.22.6 : DE LA CONVERSATION A LA MISSION IA. createMissionFromMessage : << Creer une mission avec ce message >> (le clic
    // droit sur un message envoye) ; sendAsMission : << Envoyer en mission >> (le bouton a cote d'Envoyer) - la saisie devient
    // une mission ; sendOrAdvise : un envoi du logiciel (<< Proposer la suite >>) - le conseil d'abord si la demande est
    // grosse (la carte : Creer la mission, Continuer ici). Rien n'est cree sans un clic. missionAdviceShown : la carte du
    // conseil (le mode << Continuer ici >> de la carte de choix).
    bool               createMissionFromMessage(int message);
    bool               sendAsMission();
    bool               sendOrAdvise(const std::string& text);
    [[nodiscard]] bool missionAdviceShown() const noexcept;
    [[nodiscard]] ui::Button& missionSendButton() noexcept { return *missionSendBtn_; }
    // Le tableau de bord au centre (UiHost::openMission) ; `mode` -1 : le tableau de bord, ou l'ecran de reprise si la
    // mission en a besoin (en pause, un appel interrompu, le projet modifie ou introuvable) ; 0 : le tableau de bord ;
    // 1 : l'ecran de reprise ; 2 : l'historique. Sans hote (un essai) : l'onglet Missions IA, la mission choisie. Faux :
    // la mission n'existe plus (ou l'hote n'a pas pu l'ouvrir).
    bool openMission(const std::string& missionId, int mode = -1);
    // UN GESTE SUR UNE MISSION (la liste, la bande, le tableau de bord) : l'Assistant le fait, la barre d'etat dit ce
    // qui s'est passe (ou pourquoi rien). Cancel : une question d'abord. Conversation : sa conversation ouverte ici.
    // 1.22.2 : ForceFinish (Forcer la fin de la mission...) - refusee tout de suite et pourquoi (Assistant::canForceFinish),
    // sinon une question d'abord (missionui::forceFinishQuestion : ce qui va se passer), puis Assistant::forceFinishMission.
    enum class MissionAct : std::uint8_t { Continue, Correct, Refresh, Audit, Pause, Resume, Cancel, Archive, Unarchive, Conversation, ForceFinish };
    bool missionAct(const std::string& missionId, MissionAct act, const std::string& step = {});
    bool renameMission(const std::string& missionId, const std::string& name);
    // Une proposition de la mission, PAR SA CONVERSATION (rouverte) et les gestes de sa carte : 0 Previsualiser (la
    // fenetre d'apercu), 1 Verifier (sa carte dans la discussion, le plan deplie), 2 Appliquer (les confirmations de la
    // carte). Faux : introuvable (sa conversation supprimee : le tableau de bord garde son compte rendu).
    bool missionProposal(const std::string& missionId, const std::string& proposalId, int what);
    // L'IA travaille-t-elle pour cette mission ? (une reponse dans l'une de ses conversations, l'audit final)
    [[nodiscard]] bool missionBusy(const mission::Mission& m) const;
    // 1.22.2 : LES CARTES DES PROPOSITIONS d'une mission (son tableau de bord), depliees ou reduites PAR L'UTILISATEUR :
    // gardees pour la seance, par mission et par proposition, en memoire (jamais dans le fichier de la mission) ; aucune
    // valeur : le choix par defaut (missionui::cardOpenByDefault).
    [[nodiscard]] std::optional<bool> missionCard(const std::string& missionId, const std::string& proposalId) const;
    void                              setMissionCard(const std::string& missionId, const std::string& proposalId, bool open);

    [[nodiscard]] UiHost& uiHost() noexcept { return ui_; }
    // La part du separateur que l'ecran a posee pour la largeur par defaut (-1 : aucune) ; une
    // autre part est celle que l'utilisateur a tiree.
    [[nodiscard]] float autoRatio() const noexcept { return autoRatio_; }
    void setAutoRatio(float r) noexcept { autoRatio_ = r; }

    // 1.14.2 (captures, 2e passe) : un texte coupe par "..." se lit en entier dans son infobulle.
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    friend class AiChatView;
    void say(const std::string& text);
    void refreshButtons();
    void layoutRows();
    [[nodiscard]] int  rowAt(gfx::Point p) const;
    void               menuChosen(int id);

    std::unique_ptr<Assistant> assistant_;
    UiHost                     ui_;
    ui::Button*       newBtn_{nullptr};
    ui::Button*       settingsBtn_{nullptr};
    ui::Button*       closeBtn_{nullptr};
    ui::ToggleButton* modePreview_{nullptr};
    ui::ToggleButton* modeDirect_{nullptr};
    ui::InputText*    rename_{nullptr};
    AiChatView*       chat_{nullptr};
    AiPlanStrip*      strip_{nullptr};   // 1.20.2
    float             stripH_{0.f};
    AiContextPane*    ctxPane_{nullptr};   // 1.21.0 : le contexte de la discussion (les fichiers joints)
    float             ctxH_{0.f};
    void              contextAction(int what, const std::string& attachmentId);
    AiPromptEdit*     prompt_{nullptr};
    ui::Button*       attachBtn_{nullptr};
    ui::Button*       sendBtn_{nullptr};
    ui::PopupMenu*    menu_{nullptr};

    // La geometrie de la derniere mise en page.
    gfx::Rect              header_{}, statusLine_{}, modeRow_{}, listHead_{}, list_{}, pendingArea_{}, promptArea_{}, bottomBar_{};
    gfx::Rect              outgoing_{};             // 1.15.0 : ce qui part (sous la saisie)
    std::vector<std::string> outgoingLines_;
    std::string            outgoingText_;
    std::vector<gfx::Rect> rows_;            // une par conversation montree
    std::vector<gfx::Rect> pendingRemove_;   // la croix de chaque piece en attente
    float                  listScroll_{0.f};
    ui::EdgeScrollBar      listBar_;
    bool                   listOpen_{true};
    int                    hoverRow_{-1};
    int                    renameIndex_{-1};
    int                    menuRow_{-1};
    std::uint64_t          seen_{0};         // la revision de la seance deja montree
    std::string            lastStatus_;
    std::string            appliedKey_;      // l'etat reel des propositions appliquees deja montre
    double                 appliedCheck_{-1.0};
    std::string            watchingReply_;   // 1.16.1 : la conversation de la reponse en cours (l'avertissement)
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;   // les textes coupes du dernier dessin
    float                  autoRatio_{-1.f};
    bool                   syncing_{false};
    bool                   loaded_{false};
    core::ConnectionScope  links_;
    // 1.16.0 (FOURNISSEURS) : la bande de la limite (sa place, ses lignes a la largeur, ce qu'elle dit).
    void                     refreshBand(double now);
    gfx::Rect                limitLine_{};
    UsageBand                band_;
    std::vector<std::string> bandLines_;
    std::string              bandKey_;
    double                   bandAt_{-1.0};   // 1.22.6 : pendant une reponse, refaite quatre fois par seconde au plus
    // 1.21.2 : l'onglet Notifications (les deux onglets sous l'en-tete, la vue du journal, la pastille).
    void                     syncNotifications();                         // le journal a change : la vue, la pastille
    [[nodiscard]] bool       looking(const std::string& conversation) const;   // cette conversation est-elle a l'ecran ?
    void                     notifyReply(const Conversation& c);          // une reponse prete hors de la vue
    AiNotificationsView*     notifView_{nullptr};
    bool                     notifsShown_{false};
    gfx::Rect                tabs_{}, tabConv_{}, tabNotifs_{};
    std::uint64_t            notifsSeen_{~0ull};
    std::size_t              badge_{0};
    bool                     badgeError_{false};
    std::string              latest_;                                     // la plus recente (l'infobulle de l'onglet)
    int                      hoverTab_{-1};
    // 1.22.0 : les missions IA (l'onglet, la bande, la carte de choix).
    void                     syncMissions();                              // la liste, la bande, la pastille de l'onglet
    void                     offerMission(const std::string& text);       // la carte << Creer une mission IA ? >>
    AiMissionsView*          missionsView_{nullptr};
    AiMissionStrip*          missionStrip_{nullptr};
    AiMissionAsk*            missionAsk_{nullptr};
    bool                     missionsShown_{false};
    gfx::Rect                tabMissions_{};
    bool                     notifsCompact_{false}, missionsCompact_{false};   // les onglets etroits : l'icone et la pastille
    std::size_t              missionsBadge_{0};
    bool                     missionsAlert_{false};
    float                    missionStripH_{0.f}, askH_{0.f}, chatBottom_{0.f};
    std::string              askText_;                                    // la demande qui attend le choix (vide : aucune)
    std::map<std::string, std::map<std::string, bool>> missionCards_;     // 1.22.2 : mission -> proposition -> depliee
    // 1.22.5 : LA PREVISUALISATION OUVERTE (sa proposition) - un nouvel avis du logiciel lui est passe (AiPreviewDialog::
    // setVerdict) ; `previewAlive_` tombe a faux quand la fenetre se ferme (son rappel de fermeture).
    AiPreviewDialog*         preview_{nullptr};
    std::shared_ptr<bool>    previewAlive_;
    std::string              previewProposal_;
    std::uint64_t            verdictSeen_{0};
    void                     trackPreview(AiPreviewDialog* dialog, const std::string& proposalId);
    void                     pushVerdict();
    // 1.22.6 (a la fin) : DE LA CONVERSATION A LA MISSION IA - le bouton << Envoyer en mission >> ; la carte du conseil (le
    // message de l'utilisateur qu'il vise : -1, la demande pas encore envoyee) ; le conseil d'une reponse deja montre ou ferme
    // par la croix (conversation#message : il ne revient pas).
    ui::Button*              missionSendBtn_{nullptr};
    int                      askMessage_{-1};
    std::string              adviceSeen_;
    void                     offerAdvice(const std::string& text, const std::string& reason, int message);
    void                     syncAdvice();
};

// =============================================================================
//  La discussion : les messages, la carte des propositions, l'indicateur.
// =============================================================================
class AiChatView final : public ui::Widget {
public:
    AiChatView(std::string id, AiPanel& panel);

    void rebuild();                          // la conversation a change : les cartes, les boutons
    void scrollToBottom();
    // 1.14.2 (captures, 2e passe) : une conversation qu'on ouvre montre son debut si elle tient,
    // sinon le debut de son dernier echange (la derniere demande en haut) - pas sa fin.
    void showLastExchange();
    void scrollBy(float dy);
    [[nodiscard]] float scrollOffset() const noexcept { return offset_; }
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    [[nodiscard]] bool  atBottom() const noexcept;
    // La carte d'un message (vide : pas de carte, ou pas encore dessinee), ses boutons.
    [[nodiscard]] gfx::Rect cardRect(int message) const;
    // 0 Previsualiser, 1 Appliquer, 2 Refuser / Redemander ; 1.22.4 (retenue) : 3 Ignorer cette etape, 4 Forcer la fin de la mission
    [[nodiscard]] ui::Button* cardButton(int message, int which) const;
    // 1.15.0 : Reessayer, sous le dernier message (une erreur, un arret) ; nul : pas encore cree.
    [[nodiscard]] ui::Button* retryButton() const noexcept { return retry_; }
    [[nodiscard]] gfx::Rect suggestionRect(std::size_t index) const;
    [[nodiscard]] std::size_t suggestionCount() const noexcept { return suggestionRects_.size(); }
    // Le texte d'un message tel qu'il est mis en page (essais) : ses lignes.
    [[nodiscard]] std::vector<std::string> renderedLines(int message) const;

    // Une ligne de texte mis en forme : des morceaux poses (gras, code, titre, puce).
    struct Run {
        std::string text;
        float       x{0.f}, w{0.f};
        std::uint8_t style{0};   // 0 normal, 1 gras, 2 code, 3 titre, 4 attenue, 5 ligne d'un bloc de code
    };
    struct Line {
        std::vector<Run> runs;
        float            h{0.f};
        float            gapBefore{0.f};
        std::string      bullet;     // "\xE2\x80\xA2", "1."
        float            bulletX{0.f};
        // 1.20.2 : LES BLOCS DE CODE (```lang ... ```) - montres a part, avec la langue et un bouton Copier. La ligne
        // d'en-tete porte la langue et le code exact (sans les balises) ; les lignes du bloc ont le meme `code`.
        int              code{-1};          // l'index du bloc dans le message ; -1 : du texte
        bool             codeHead{false};   // la ligne d'en-tete du bloc
        bool             codeOpen{false};   // le bloc n'est pas encore ferme (la reponse arrive)
        std::string      codeLang, codeText;
    };
    [[nodiscard]] static std::vector<Line> layoutText(const std::string& markdown, float width);
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }
    // 1.20.2 : LE PLAN EN COURS (la bande du haut, AiPlanStrip) - la proposition qui se verifie ou s'applique sur un
    // fil de fond, sinon la derniere de la conversation qui a un plan (a valider, appliquee) ; rien : nullopt.
    struct LivePlan {
        int               message{-1};
        std::string       id;               // la proposition
        const plan::Plan* plan{nullptr};    // ses etapes (lues aussitot : valides jusqu'au prochain changement)
        std::string       head;             // << Plan d'action - 12 etapes, 12 validees >>
        std::string       phases;           // << Analyse > Planification > Verification > ... >>
        std::string       status;           // << Application... >>, << Verification... >>, << Validation requise >>...
        std::uint8_t      tone{0};          // 0 info, 1 ok, 2 attention, 3 erreur
        float             progress{-1.f};   // 0..1 : le travail de fond ; -1 : aucun
        bool              applying{false}, checking{false}, open{false};   // open : a decider (proposee)
        std::size_t       ops{0};
        // 1.22.5 (a la fin) : LE PROCESSUS DE DEVELOPPEMENT - sa derniere etape (Assistant::processHeadline), et s'il travaille
        // encore (Generer et Compiler, une correction automatique) : la bande se montre alors meme sans plan a montrer.
        std::string       process;
        bool              working{false};
    };
    [[nodiscard]] std::optional<LivePlan> livePlan() const;
    // Fait defiler jusqu'a la carte de ce message (`openPlan` : son plan deplie).
    void revealMessage(int message, bool openPlan);
    // 1.20.2 : les boutons Copier des blocs de code du dernier dessin (leur place, le code qu'ils copient) ; le code
    // copie en dernier (vide : aucun, ou plus de 2 s : la pastille redit << Copier >>).
    [[nodiscard]] const std::vector<std::pair<gfx::Rect, std::string>>& copyButtons() const noexcept { return copySpots_; }
    [[nodiscard]] bool copyFlash() const noexcept { return !copied_.empty(); }
    bool copyCode(const std::string& code);
    // 1.22.1 : LE CLIC DROIT SUR UNE ERREUR DE LA CARTE (AiFixes.hpp) - les notes de la carte d'un message (erreurs puis
    // avertissements, les doublons regroupes), le menu d'une note (au clic droit ; ici : sur la note dessinee, sinon au
    // milieu de la discussion), ses entrees, en choisir une comme un clic (essais, scripts).
    [[nodiscard]] std::vector<std::string> cardNotes(int message) const;
    bool openNoteMenu(int message, std::size_t note);
    [[nodiscard]] std::vector<std::string> noteMenuItems() const;
    bool chooseNoteMenuItem(const std::string& label);
    // 1.22.5 : LE PROCESSUS DE DEVELOPPEMENT d'une carte - deplie ou non (par defaut : deplie pendant le travail), le deplier
    // ou le replier (un clic sur son en-tete), ses lignes telles qu'elles sont mises en page (l'en-tete, puis les lignes si
    // deplie), la place de son en-tete (vide : pas de processus) ; un clic sur un constat (son rang parmi les lignes) - la
    // previsualisation de sa proposition.
    [[nodiscard]] bool processOpen(int message) const;
    void setProcessOpen(int message, bool open);
    [[nodiscard]] std::vector<std::string> processLines(int message) const;
    [[nodiscard]] gfx::Rect processRect(int message) const;
    bool clickProcessLine(int message, std::size_t line);
    // 1.22.6 : CE QUE FAIT L'IA (Assistant::activity) - les lignes de l'activite telles qu'elles sont montrees (le contexte, la
    // ligne, puis les etapes passees si l'historique est deplie), son icone (un clic replie ou deplie l'historique) ; LE BILAN
    // d'une reponse (le message de la conversation ouverte, celui de la reponse en cours compris) : sa ligne, son << + >> (la
    // place au dernier dessin ; vide : pas de bilan), deplie ou non, ses lignes de detail. Les essais s'en servent.
    [[nodiscard]] std::vector<std::string> activityLines() const;
    [[nodiscard]] gfx::Rect                activityToggleRect() const noexcept { return historySpot_; }
    [[nodiscard]] bool                     activityHistoryOpen() const noexcept { return historyOpen_; }
    void                                   setActivityHistoryOpen(bool open);
    [[nodiscard]] std::string              billText(int message) const;
    [[nodiscard]] std::vector<std::string> billDetailLines(int message) const;
    [[nodiscard]] gfx::Rect                billRect(int message) const;
    [[nodiscard]] bool                     billOpen(int message) const;
    void                                   setBillOpen(int message, bool open);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    // 1.22.1 : le menu du clic droit des notes (cree au premier usage), ses actions par id, la carte visee ; la place de
    // chaque note au dernier dessin.
    bool openNoteMenuAt(int message, std::size_t note, gfx::Point at);
    void runNoteFix(int id);
    ui::PopupMenu*             fixMenu_{nullptr};
    std::vector<fixes::Action> fixActions_;
    int                        fixMessage_{-1};
    struct NoteSpot {
        gfx::Rect   at;
        int         message{-1};
        std::size_t note{0};
    };
    std::vector<NoteSpot>      noteSpots_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> copySpots_;
    mutable std::string copied_;
    mutable double      copiedAt_{-1.0};
    // 1.22.6 : l'activite lue au plus kRefresh fois par seconde (la mise en page la relit) ; l'historique deplie ; les bilans
    // deplies (conversation|message) ; les places du dernier dessin (le << + >> de chaque bilan, l'icone de l'historique).
    [[nodiscard]] const activity::Snapshot& activityNow(bool fresh) const;
    [[nodiscard]] std::string billKey(int message) const;
    mutable activity::Snapshot                 act_;
    mutable double                             actAt_{-1e9};
    mutable activity::Bill                     liveBill_;
    bool                                       historyOpen_{true};
    std::set<std::string>                      billOpen_;
    std::vector<std::pair<gfx::Rect, int>>     billSpots_;
    gfx::Rect                                  historySpot_{};
    bool                                       actRemeasure_{false};   // la ligne demande une rangee de plus : la mise en page suit
    double                                     actRemeasureAt_{-1e9};  // ... une fois par lecture de la ligne (kRefresh)
    struct Block {                 // un message mis en page
        int               message{-1};
        bool              user{false};
        float             y{0.f}, h{0.f};
        std::vector<Line> lines;
        float             textW{0.f};
        gfx::Rect         bubble{};       // relatif au haut du bloc
        gfx::Rect         card{};         // relatif au haut du bloc (vide : pas de carte)
        float             attachH{0.f};
        // 1.14.2 : la carte mise en page a sa largeur (rien n'est coupe par "...") : ses notes
        // (erreurs, avertissements) et sa ligne d'etat a la ligne, ses boutons poses de gauche a
        // droite et a la ligne quand ils ne tiennent pas (relatifs au bas de la carte), et l'etat
        // reel d'une proposition appliquee (Ctrl+Z).
        // 1.22.1 (a la fin) : `message` - le message de la validation (sans le << (2 operations) >> d'un doublon) : le clic
        // droit retrouve sa ligne (AiFixes) et ses corrections.
        struct Note { bool error{false}; std::vector<std::string> lines; std::string message; };
        std::vector<Note>        notes;
        std::vector<std::string> doneLines;
        std::vector<std::string> buttons;
        std::vector<gfx::Rect>   buttonRects;
        float                    buttonsH{0.f};
        Assistant::Applied       applied{Assistant::Applied::NotApplied};
        // 1.15.0 : la note du logiciel (une erreur, une reponse coupee) a la ligne, et la place
        // de Reessayer (relatif au haut du bloc ; 0 : pas de bouton).
        std::vector<std::string> noteLines;
        bool                     noteError{false};
        float                    noteY{0.f}, retryY{0.f};
        // 1.16.0 : ce que la proposition cree en plus des objets (la vue, les variables, les types), sous les comptes.
        std::vector<std::string> createLines;
        // 1.19.0 : LE PLAN D'ACTION (une demande qui en merite un : plan::Plan::worthShowing) - les phases reelles, l'en-tete
        // (un clic le deplie), une ligne par etape dans l'ordre d'execution avec son etat ; apres Appliquer, le compte
        // rendu reel. Les hauteurs sont relatives au haut de la carte.
        std::vector<std::string> phaseLines;
        std::string              planHead;
        float                    planY{0.f};       // l'en-tete du plan (0 : pas de plan affiche)
        struct StepLine {
            std::string text;
            plan::State state{plan::State::ToDo};
            std::size_t op{0};
            bool        removable{false};          // proposee, facultative ou retiree : << retirer >> / << remettre >>
            bool        excluded{false};
            std::string kind, name, view;          // ce qu'un clic ouvre (appliquee)
        };
        std::vector<StepLine>    steps;
        float                    stepsY{0.f};
        std::string              stepsMore;        // "... et 12 autres etapes"
        struct ReportLine { std::string text; std::uint8_t tone{0}; };   // 0 normal, 1 ok, 2 attention, 3 erreur, 4 titre
        std::vector<ReportLine>  reportLines;
        // 1.22.4 (a la fin) : NE PAS TOURNER EN ROND - le cercle vu a l'arrivee (Assistant::loopOf), sous les comptes ;
        // retenue : les boutons sont les choix (Corriger a la main, Proposer quand meme, Modifier la demande ; une mission :
        // Ignorer cette etape, Forcer la fin de la mission).
        std::vector<std::string> loopLines;
        bool                     loopHeld{false};
        // 1.22.5 (a la fin) : LE PROCESSUS DE DEVELOPPEMENT (Assistant::processOf) - l'en-tete (un clic le deplie ; deplie
        // pendant que le logiciel ou l'IA travaillent), ses lignes (l'heure, ce qui s'est passe) et, sous l'avis de Generer et
        // Compiler, ses constats (rouge : une erreur, ambre : un avertissement ; un clic : la previsualisation). Les hauteurs
        // sont relatives au haut de la carte.
        struct ProcLine {
            std::string  text;
            std::uint8_t tone{0};          // 0 normal, 1 ok, 2 attention (ambre), 3 erreur (rouge), 4 attenue
            bool         finding{false};   // un constat (decale, cliquable)
            std::string  proposal;         // la proposition qu'il concerne (Proposal::id)
        };
        std::vector<ProcLine>    procLines;
        std::string              procHead;
        float                    procY{0.f};    // l'en-tete (0 : pas de processus)
        bool                     procOpen{false};
        // remplacee par sa correction (l'avis du logiciel) : le message de la proposition corrigee (-1 : pas encore la)
        int                      replacedBy{-1};
        // 1.22.6 (a la fin) : CE QUE FAIT L'IA (Assistant::activity) - sous l'indicateur de la reponse en cours, ou sous une
        // reponse dont la proposition se verifie : le contexte (une mission, une correction), la ligne, les etapes passees
        // montrees ; ce que l'indicateur dit en plus (une nouvelle tentative) ; LE BILAN (Assistant::billOf, liveBill) : sa
        // ligne et son << + >>, son detail deplie. Hauteurs relatives au haut du bloc (0 : rien).
        float                    actY{0.f};
        std::size_t              actRows{1};     // la ligne d'activite, a la ligne (deux au plus)
        bool                     actContext{false};
        std::size_t              actHistory{0};
        bool                     showDetail{true};
        float                    billY{0.f};
        std::string              billText;
        std::vector<std::string> billLines;
    };
    struct CardButtons {
        int         message{-1};
        ui::Button* preview{nullptr};
        ui::Button* apply{nullptr};
        ui::Button* third{nullptr};       // Refuser, ou Redemander (perimee)
        bool        stale{false};
        bool        undone{false};        // appliquee puis annulee : `apply` dit Reappliquer
        bool        refused{false};       // 1.16.1 : refusee : `apply` dit Revenir sur le refus
        bool        detect{false};        // 1.18.0 : appliquee : `apply` dit Detecter ce qui repond...
        bool        next{false};          // 1.21.3 : appliquee, une suite prevue : `apply` dit Proposer la suite
        // 1.22.4 (a la fin) : retenue (elle tourne en rond) - `preview` Corriger a la main, `apply` Proposer quand meme,
        // `third` Modifier la demande ; une mission : `fourth` Ignorer cette etape, `fifth` Forcer la fin de la mission.
        ui::Button* fourth{nullptr};
        ui::Button* fifth{nullptr};
        bool        loop{false};
        // 1.22.5 (a la fin) : remplacee par sa correction - `apply` dit << Voir la correction >> (la carte de la corrigee).
        int         replacedBy{-1};
    };
    void measure();
    void placeCards();             // les boutons des cartes, au decalage du moment
    void applyAnchor();            // showLastExchange, une fois la largeur connue
    void paintCard(const ui::PaintContext&, const Block&, gfx::Rect card);
    void paintIndicator(const ui::PaintContext&, gfx::Rect at);
    void paintActivity(const ui::PaintContext&, const Block&, float top);   // 1.22.6 : ce que fait l'IA
    void paintBill(const ui::PaintContext&, const Block&, float top);       // 1.22.6 : le bilan, son << + >>
    [[nodiscard]] float viewHeight() const noexcept;

    AiPanel&                 panel_;
    std::vector<Block>       blocks_;
    std::vector<CardButtons> cards_;
    std::vector<gfx::Rect>   suggestionRects_;
    ui::Button*              retry_{nullptr};
    float                    suggestionsTop_{0.f};
    float                    offset_{0.f};
    float                    content_{0.f};
    float                    measuredW_{-1.f};
    std::uint64_t            measuredRev_{0};
    bool                     stick_{true};
    bool                     anchor_{false};   // showLastExchange en attente d'une largeur
    bool                     anchored_{false}; // place par showLastExchange, pas encore defile a la main : suit la largeur
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
    int                      hoverSuggestion_{-1};
    ui::EdgeScrollBar        bar_;
    core::ConnectionScope    links_;
    // L'apercu de chaque carte (la copie de la vue, refaite quand la vue change).
    // 1.20.0 : une GROSSE proposition se verifie sur un fil de fond (la copie du projet, l'environnement fige) - la carte
    // montre l'avancement, l'interface ne gele pas ; le resultat arrive au tick (pollThumbs).
    struct ThumbJob {
        std::string              key;
        std::atomic<std::size_t> done{0};
        std::size_t              total{0};
        std::atomic<bool>        finished{false};
        Preview                  preview;
        Check                    check;
        plan::Plan               plan;
        std::thread              worker;
    };
    struct Thumb {
        std::string key;      // la proposition, l'empreinte de la vue
        Preview     preview;
        Check       check;
        plan::Plan  plan;     // 1.19.0 : le plan d'action (celui de la validation, ou celui garde a l'application)
        // 1.20.0 : l'empreinte n'est relue que si le projet a change (ou chaque seconde), pas a chaque peinture.
        std::string   base, print;
        std::uint64_t revision{~0ull};
        double        at{-1e9};
        std::shared_ptr<ThumbJob> job;   // la verification de fond en cours (nul : aucune)
        [[nodiscard]] bool pending() const noexcept { return job != nullptr; }
    };
    std::set<std::string> planOpen_;   // 1.19.0 : les propositions dont le plan est deplie (par identifiant)
    std::map<std::string, bool> procOpen_;   // 1.22.5 : le processus deplie / replie a la main (sinon : deplie pendant le travail)
    mutable std::map<int, Thumb> thumbs_;
    mutable std::vector<std::shared_ptr<ThumbJob>> retiring_;   // remplacees en cours : attendues sans bloquer
    const Thumb& thumb(int message) const;

public:
    ~AiChatView() override;
    // 1.20.0 : les verifications de fond finies passent dans leur carte (vrai : une carte a change).
    bool pollThumbs();
    // Une verification de fond est en cours pour cette carte (essais) ; son avancement (0..1).
    [[nodiscard]] bool  checking(int message) const;
    [[nodiscard]] float checkProgress(int message) const;
    // 1.20.1 : le resultat de la verification de cette carte, s'il est la (faux : en cours sur le fil de fond) - la
    // previsualisation et Appliquer le reprennent au lieu de tout revalider sur le fil de l'interface.
    bool resultReady(int message, Check* check, Preview* preview, plan::Plan* plan) const;
    [[nodiscard]] bool  anyChecking() const {
        for (const auto& [m, t] : thumbs_)
            if (t.pending()) return true;
        return false;
    }
    // 1.22.6 : LE CLIC DROIT SUR UN MESSAGE ENVOYE - le menu << Creer une mission avec ce message >> (au clic droit sur sa
    // bulle ; ici : au milieu de la bulle), ses entrees, en choisir une comme un clic (essais, scripts) ; la place d'une bulle
    // de l'utilisateur au dernier dessin (vide : pas une bulle de l'utilisateur, ou hors de la vue).
    bool                                   openMessageMenu(int message);
    [[nodiscard]] std::vector<std::string> messageMenuItems() const;
    bool                                   chooseMessageMenuItem(const std::string& label);
    [[nodiscard]] gfx::Rect                bubbleRect(int message) const;
    // La place d'un message au dernier dessin (toute sa largeur ; vide : hors de la vue) - << Mission << X >> creee - Ouvrir >>.
    [[nodiscard]] gfx::Rect                messageRect(int message) const;

private:
    bool         openMessageMenuAt(int message, gfx::Point at);
    ui::PopupMenu* msgMenu_{nullptr};
    int          msgMenuMessage_{-1};
};

} // namespace app::ai
