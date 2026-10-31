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

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiChatView;

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
        std::uint8_t style{0};   // 0 normal, 1 gras, 2 code, 3 titre, 4 attenue
    };
    struct Line {
        std::vector<Run> runs;
        float            h{0.f};
        float            gapBefore{0.f};
        std::string      bullet;     // "\xE2\x80\xA2", "1."
        float            bulletX{0.f};
    };
    [[nodiscard]] static std::vector<Line> layoutText(const std::string& markdown, float width);
    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
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
    };
    struct CardButtons {
        int         message{-1};
        ui::Button* preview{nullptr};
        ui::Button* apply{nullptr};
        ui::Button* third{nullptr};       // Refuser, ou Redemander (perimee)
        bool        stale{false};
        bool        undone{false};        // appliquee puis annulee : `apply` dit Reappliquer
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
    struct Thumb {
        std::string key;      // la proposition, l'empreinte de la vue
        Preview     preview;
        Check       check;
    };
    mutable std::map<int, Thumb> thumbs_;
    const Thumb& thumb(int message) const;
};

} // namespace app::ai
