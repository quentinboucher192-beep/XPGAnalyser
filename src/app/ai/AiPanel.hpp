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
// =============================================================================
#pragma once

#include "AiAssistant.hpp"
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
    [[nodiscard]] ui::Button* cardButton(int message, int which) const;   // 0 Previsualiser, 1 Appliquer, 2 Refuser / Redemander
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
    };
    [[nodiscard]] std::optional<LivePlan> livePlan() const;
    // Fait defiler jusqu'a la carte de ce message (`openPlan` : son plan deplie).
    void revealMessage(int message, bool openPlan);
    // 1.20.2 : les boutons Copier des blocs de code du dernier dessin (leur place, le code qu'ils copient) ; le code
    // copie en dernier (vide : aucun, ou plus de 2 s : la pastille redit << Copier >>).
    [[nodiscard]] const std::vector<std::pair<gfx::Rect, std::string>>& copyButtons() const noexcept { return copySpots_; }
    [[nodiscard]] bool copyFlash() const noexcept { return !copied_.empty(); }
    bool copyCode(const std::string& code);

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    mutable std::vector<std::pair<gfx::Rect, std::string>> copySpots_;
    mutable std::string copied_;
    mutable double      copiedAt_{-1.0};
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
        struct Note { bool error{false}; std::vector<std::string> lines; };
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
    };
    void measure();
    void placeCards();             // les boutons des cartes, au decalage du moment
    void applyAnchor();            // showLastExchange, une fois la largeur connue
    void paintCard(const ui::PaintContext&, const Block&, gfx::Rect card);
    void paintIndicator(const ui::PaintContext&, gfx::Rect at);
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
};

} // namespace app::ai
