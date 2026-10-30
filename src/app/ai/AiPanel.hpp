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
#include <vector>

namespace app::ai {

class AiChatView;

// Le champ de la demande : un editeur de texte multiligne du logiciel, ou Entree envoie.
class AiPromptEdit final : public ui::MultiLineText {
public:
    explicit AiPromptEdit(std::string id);
    const core::SignalPtr<> submitted = core::Signal<>::create();
protected:
    ui::EventResult onEvent(const ui::InputEvent&) override;
};

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
    bool refuseProposal(int message);
    void editRequest(int message);                 // Modifier la demande : le texte remis dans la saisie
    void openSettings();
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
    [[nodiscard]] gfx::Rect      chatRect() const;
    [[nodiscard]] std::string    lastStatus() const { return lastStatus_; }
    // Le menu du clic droit d'une conversation (Renommer, Supprimer...).
    void openRowMenu(int index, gfx::Point at);
    enum MenuId : int { kMenuOpen = 1, kMenuRename, kMenuDelete, kMenuNew };

    [[nodiscard]] UiHost& uiHost() noexcept { return ui_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    void onPaintOverlay(const ui::PaintContext&) override;
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
    bool                   syncing_{false};
    bool                   loaded_{false};
    core::ConnectionScope  links_;
};

// =============================================================================
//  La discussion : les messages, la carte des propositions, l'indicateur.
// =============================================================================
class AiChatView final : public ui::Widget {
public:
    AiChatView(std::string id, AiPanel& panel);

    void rebuild();                          // la conversation a change : les cartes, les boutons
    void scrollToBottom();
    void scrollBy(float dy);
    [[nodiscard]] float scrollOffset() const noexcept { return offset_; }
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    [[nodiscard]] bool  atBottom() const noexcept;
    // La carte d'un message (vide : pas de carte, ou pas encore dessinee), ses boutons.
    [[nodiscard]] gfx::Rect cardRect(int message) const;
    [[nodiscard]] ui::Button* cardButton(int message, int which) const;   // 0 Previsualiser, 1 Appliquer, 2 Refuser / Redemander
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
    };
    struct CardButtons {
        int         message{-1};
        ui::Button* preview{nullptr};
        ui::Button* apply{nullptr};
        ui::Button* third{nullptr};       // Refuser, ou Redemander (perimee)
        bool        stale{false};
    };
    void measure();
    void paintCard(const ui::PaintContext&, const Block&, gfx::Rect card);
    void paintIndicator(const ui::PaintContext&, gfx::Rect at);
    [[nodiscard]] float viewHeight() const noexcept;

    AiPanel&                 panel_;
    std::vector<Block>       blocks_;
    std::vector<CardButtons> cards_;
    std::vector<gfx::Rect>   suggestionRects_;
    float                    suggestionsTop_{0.f};
    float                    offset_{0.f};
    float                    content_{0.f};
    float                    measuredW_{-1.f};
    std::uint64_t            measuredRev_{0};
    bool                     stick_{true};
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
