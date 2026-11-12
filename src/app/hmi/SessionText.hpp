// =============================================================================
//  app/hmi/SessionText.hpp - 1.47.0 : LE TEXTE DE LA SESSION, DANS SON CADRE
// -----------------------------------------------------------------------------
//  Le client (10/11), sur une capture de la fenetre de la session (vue Journal,
//  une fenetre de 943 x 609 : << Aucun changement encore : chaque changement de
//  la session s'inscrit ici... >> sortait a droite) : << bug a corriger, le
//  texte sort du cadre, attention au redimensionnement >>.
//
//  CE QUI SERT A TOUT CE QUI MONTRE LA SESSION (l'onglet << Session >> du
//  panneau du bas, la fenetre de la session) :
//   - wrap() : un texte en lignes qui tiennent dans une largeur - coupe aux
//     espaces ; un mot plus long que la ligne (un nom d'objet, un chemin) est
//     coupe entre deux caracteres, jamais au milieu d'un caractere UTF-8 ;
//   - elide() : une ligne qui ne tient pas finit par << ... >> (le texte entier
//     va dans l'infobulle) ;
//   - fitButtons() : une rangee de boutons dans une largeur - leur libelle
//     entier, sinon le court, sinon raccourci avec << ... >> (l'infobulle dit le
//     libelle entier) ; ce qui ne tient toujours pas passe a la ligne suivante ;
//   - LineList : la discussion et le journal - chaque ligne retournee a la ligne
//     a la largeur du moment, une barre de defilement (ui::ScrollArea : la
//     molette, le pouce qu'on tire), la derniere ligne en bas tant qu'on n'est
//     pas remonte lire plus haut ; l'infobulle d'une ligne : qui (le nom entier),
//     quand, la discussion ou le journal.
//  Les mesures : ui::measureWidth (celles du dessin).
//
//  1.50.0 - le client : << faire en sorte de pouvoir repondre a des messages
//  specifiques dans le chat, pouvoir les deballer/reduire, pouvoir les copier
//  dans le presse-papier >>. La liste (l'onglet << Session >> et la fenetre de
//  la session : la meme) :
//   - REPONDRE : le survol d'un message montre << Repondre >> et << Copier >>
//     (deux pictogrammes en haut a droite) ; le clic droit aussi. Le conteneur
//     montre << En reponse a Lea : << debut du message... >> >> au-dessus du
//     champ (ReplyBar, sa croix l'annule) ; la reponse porte l'identifiant du
//     message (session::ChatReply). Une reponse montre l'extrait cite au-dessus
//     de son texte ; un clic dessus va au message cite et le choisit ;
//   - REDUIRE / DEBALLER : un message de plus de kCollapseLines + 1 lignes (a
//     la largeur du moment) montre ses kCollapseLines premieres lignes puis
//     << ... voir tout >> ; deballe : tout, puis << Reduire >>. Le clic droit :
//     Deballer / Reduire, et << Tout reduire >>. L'etat suit le message (son
//     identifiant) quand la liste change ;
//   - COPIER : << Copier >> (le survol, le clic droit, Ctrl+C sur le message
//     choisi - un clic le choisit) met le texte seul dans le presse-papiers ;
//     << Copier avec l'auteur et l'heure >> : << 14:32 Lea Essai : bonjour >>.
//  Tout reste dans le cadre (la regle de la 1.47) : l'extrait et << voir tout >>
//  tiennent sur une ligne (raccourcis avec << ... >>), les pictogrammes du
//  survol sont dans la ligne.
// =============================================================================
#pragma once

#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace app::sessionui {

inline constexpr gfx::FontId kText{13};       // les lignes, les textes
inline constexpr gfx::FontId kTextBold{13, 1};
inline constexpr gfx::FontId kButtonFont{16};  // celle de ui::Button
inline constexpr float kButtonPad = 12.f;      // la marge du libelle, de chaque cote

// Les lignes de `text` dans `width` (les retours a la ligne gardes ; un mot trop long coupe). Vide : texte vide.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, gfx::FontId font, float width);
// Le texte s'il tient, sinon son debut et << ... >> ; vide si meme << ... >> ne tient pas.
[[nodiscard]] std::string elide(std::string_view text, gfx::FontId font, float width);
[[nodiscard]] bool fits(std::string_view text, gfx::FontId font, float width);

// La largeur d'un bouton qui montre tout `text`.
[[nodiscard]] float buttonWidth(std::string_view text, float minimum = 0.f);

// Un bouton d'une rangee : son libelle entier, et un plus court (facultatif : << Exporter (CSV) >>).
struct ButtonSpec {
    ui::Button* button{nullptr};
    std::string full, brief;
    float       minWidth{44.f};   // en dessous : le libelle raccourci avec << ... >>
    std::string tip;              // l'infobulle d'origine (le libelle entier s'y ajoute quand il est raccourci)
};
// Pose les boutons de gauche a droite (`rightAligned` : contre le bord droit, le dernier tout a droite) dans
// `width`, sur des lignes de `height` separees de `gap` ; rend la hauteur prise (0 : aucun bouton). D'abord les
// libelles entiers, puis les courts, puis (sur une ligne) raccourcis jusqu'a leur minimum, puis sur plusieurs lignes ;
// `wrapFirst` : plusieurs lignes avant de raccourcir (les boutons d'un equipier, sous son nom). Un bouton plus large
// que la ligne est pose a la largeur de la ligne.
float fitButtons(std::vector<ButtonSpec>& specs, float x, float y, float width, float height, float gap = 6.f, bool rightAligned = false,
                 bool wrapFirst = false);
// Un seul bouton a `width` : le libelle entier, sinon le court, sinon raccourci ; l'infobulle dit le libelle entier.
void fitButton(ui::Button& b, const std::string& full, float width, const std::string& brief = {}, const std::string& tip = {});

// Le texte d'attente d'un champ (police 16, marges 8) a sa largeur : le premier de `choices` qui tient (le dernier sinon).
void fitPlaceholder(ui::InputText& field, float width, const std::vector<std::string>& choices);
// Celui du champ de la session : << Ecris a l'equipe... (Entree) >>, << Ecris a l'equipe... >>, << Ecris... >>.
[[nodiscard]] const std::vector<std::string>& messagePlaceholders();

// ---- une ligne de la discussion ou du journal ----
struct Row {
    std::string   time, who, text;      // "14:32", "Lea", "bonjour"
    std::uint32_t color{0};             // 0xRRGGBB (0 : aucune)
    bool          journal{false};       // une ligne du journal (sinon la discussion)
    bool          system{false};        // une ligne de la session (entree, depart...)
    bool          mine{false};          // ecrite par ce PC
    int           count{1};             // le journal : les gestes fondus (x3)
    // 1.50.0 : l'identifiant du message (vide : on n'y repond pas) ; le message auquel il repond (son identifiant, son
    // auteur, le debut de son texte ; vide : aucun).
    std::string   id{}, replyId{}, replyWho{}, replyText{};
};
// "14:32 Lea : bonjour (x3)" - ce que les essais lisent.
[[nodiscard]] std::string rowText(const Row&);

// ---- 1.50.0 : repondre, reduire, copier ----
inline constexpr std::size_t kCollapseLines = 4;   // un message reduit montre ses 4 premieres lignes
// Ce que << Copier >> met dans le presse-papiers : le texte seul ; avec l'auteur et l'heure : "14:32 Lea Essai : bonjour".
[[nodiscard]] std::string copyText(const Row&, bool withAuthor);
// << En reponse a Lea : << debut du message... >> >>
[[nodiscard]] std::string replyLabel(const std::string& who, const std::string& excerpt);
// Ce qui suit une ligne quand la liste change (deballee, choisie) : son identifiant, sinon l'heure, qui et le texte.
[[nodiscard]] std::string rowKey(const Row&);
// On peut repondre a cette ligne : un message de la discussion (pas le journal, pas une ligne de la session) qui a un
// identifiant.
[[nodiscard]] bool canReply(const Row&);

class LineList final : public ui::Widget {
public:
    explicit LineList(std::string id);
    // Les lignes ; la liste reste en bas si elle y etait (ou si c'est la premiere fois).
    void setRows(std::vector<Row> rows);
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }
    // Ce que la liste dit quand elle est vide (retourne a la ligne aussi).
    void setEmptyText(std::string text);
    [[nodiscard]] const std::string& emptyText() const noexcept { return empty_; }
    void scrollToBottom();
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    [[nodiscard]] float scrollOffset() const noexcept { return offset_; }
    [[nodiscard]] bool  atBottom() const noexcept;
    // Les essais : les lignes de texte posees pour la ligne `i` (apres la mise en page).
    [[nodiscard]] std::vector<std::string> wrappedText(std::size_t i) const;

    // ---- 1.50.0 : choisir, repondre, reduire / deballer, copier ----
    // Repondre propose (la discussion) ; faux : seulement Copier (le journal).
    void setRepliesEnabled(bool on);
    [[nodiscard]] bool repliesEnabled() const noexcept { return replies_; }
    // Repondre a une ligne (le survol, le clic droit) : le conteneur montre << En reponse a ... >> au-dessus du champ.
    std::function<void(const Row&)> onReply;
    // Le message choisi (un clic ; -1 : aucun) ; Ctrl+C le copie.
    [[nodiscard]] int selected() const;
    void select(int index);
    [[nodiscard]] int indexOfId(const std::string& id) const;
    // Plus de kCollapseLines + 1 lignes a la largeur du moment : reduit (sauf deballe a la main).
    [[nodiscard]] bool collapsible(std::size_t i) const;
    [[nodiscard]] bool collapsed(std::size_t i) const;
    void setExpanded(std::size_t i, bool expanded);
    void collapseAll();   // << Tout reduire >>
    // Dans le presse-papiers (copyText) ; faux : pas de telle ligne.
    bool copy(std::size_t i, bool withAuthor);
    // Repondre a la ligne i (onReply) ; faux : on n'y repond pas.
    bool reply(std::size_t i);
    // Le message cite par la ligne i : mis en vue et choisi ; faux : il n'est plus dans la liste.
    bool goToQuoted(std::size_t i);
    // Ce qui garde l'etat d'une liste refaite (la fenetre de la session refait la sienne) : les messages deballes, le
    // message choisi.
    struct ViewState {
        std::set<std::string> expanded;
        std::string           selected;
    };
    [[nodiscard]] ViewState viewState() const { return {expanded_, selectedKey_}; }
    void setViewState(ViewState v);
    // Les essais (dans le repere du widget ; vides : absents) : l'extrait cite de la ligne i, son << ... voir tout >> /
    // << Reduire >>, et les pictogrammes du survol (0 : Repondre, 1 : Copier) - ceux de la ligne survolee.
    [[nodiscard]] gfx::Rect quoteRect(std::size_t i) const;
    [[nodiscard]] gfx::Rect toggleRect(std::size_t i) const;
    [[nodiscard]] gfx::Rect hoverButton(std::size_t i, int which) const;
    [[nodiscard]] int hoveredRow() const noexcept { return hover_; }
    // Les essais : le menu du clic droit (nul : jamais ouvert) ; l'ouvrir sur la ligne i (-1 : sur le vide).
    [[nodiscard]] ui::PopupMenu* contextMenu() const noexcept { return menu_; }
    void openMenu(int row, gfx::Point at);
    void runMenu(int action);

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    struct Placed {
        float                    y{0}, h{0};      // dans le contenu (avant le decalage)
        std::string              who;             // le nom tel que montre (raccourci : << ... >>)
        bool                     whoCut{false};
        std::vector<std::string> lines;           // le texte, retourne a la ligne
        float                    textX{0}, textY{0};   // ou commence le texte (relatif a la ligne)
        // 1.50.0
        std::string              key;             // rowKey
        std::size_t              shown{0};        // les lignes montrees (reduit : kCollapseLines)
        bool                     collapsible{false}, collapsed{false};
        std::string              quote;           // l'extrait cite tel que montre (vide : pas une reponse)
        float                    quoteY{0};       // relatif a la ligne
        std::string              toggle;          // << ... voir tout >> / << Reduire >> tel que montre
        float                    toggleY{0};
    };
    void place();
    [[nodiscard]] gfx::Rect area() const noexcept;   // la zone des lignes (sans la barre)
    [[nodiscard]] int rowAt(gfx::Point p) const;      // la ligne sous `p` (-1 : aucune)
    void reveal(std::size_t i);                        // la ligne i en vue
    [[nodiscard]] int hoverButtonCount(std::size_t i) const;

    std::vector<Row>    rows_;
    std::vector<Placed> placed_;
    std::string         empty_;
    std::vector<std::string> emptyLines_;
    ui::ScrollArea      scroll_;
    float               offset_{0.f}, content_{0.f}, placedWidth_{-1.f};
    bool                stick_{true};
    bool                columns_{false};          // l'heure, qui et le texte en colonnes (assez large)
    float               whoX_{0}, whoW_{0}, textX_{0}, textW_{0};
    // 1.50.0
    bool                  replies_{true};
    std::set<std::string> expanded_;              // les messages deballes a la main (leur cle)
    std::string           selectedKey_;           // le message choisi (sa cle)
    int                   hover_{-1};             // la ligne survolee
    ui::PopupMenu*        menu_{nullptr};
    std::string           menuKey_;               // la ligne du menu ouvert (vide : le vide)
};

// ---- 1.50.0 : << En reponse a Lea : << debut du message... >> >> au-dessus du champ ----
//  Le texte (raccourci avec << ... >> a sa largeur, entier dans l'infobulle) et sa croix (annuler la reponse).
class ReplyBar final : public ui::Widget {
public:
    static constexpr float kHeight = 26.f;
    explicit ReplyBar(std::string id);
    void set(const Row& row);   // la ligne a laquelle on repond
    void clear();
    [[nodiscard]] bool active() const noexcept { return !replyId_.empty(); }
    [[nodiscard]] const std::string& replyId() const noexcept { return replyId_; }
    [[nodiscard]] const std::string& label() const noexcept { return label_; }   // le texte entier
    [[nodiscard]] const std::string& shown() const noexcept { return shown_; }   // tel qu'il tient
    [[nodiscard]] ui::Button* cancelButton() noexcept { return cancel_; }
    std::function<void()> onCancel;   // la croix (le conteneur refait sa mise en page)

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool hasTooltip() const override { return true; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    ui::Button* cancel_{nullptr};
    std::string replyId_, label_, shown_;
    gfx::Rect   textRect_{};
};

} // namespace app::sessionui
