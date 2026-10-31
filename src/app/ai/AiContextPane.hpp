// =============================================================================
//  app/ai/AiContextPane.hpp - 1.21.0 : LE VOLET << CONTEXTE DE LA DISCUSSION >>
// -----------------------------------------------------------------------------
//  Une bande au-dessus de la discussion (comme la bande du plan, AiPlanStrip), qui
//  ne defile pas avec elle. Repliee : une ligne - << Contexte de la discussion >>
//  et le compteur << Fichiers joints : 4 >> (les manquants dits en rouge).
//  Depliee (un clic) : la section FICHIERS JOINTS, un fichier par ligne - son
//  icone selon le format, son nom, son type, sa taille, sa date d'ajout, sa
//  disponibilite (Disponible, Deplace, Manquant) et son etat d'analyse (Analyse,
//  En cours, Echec, Non analyse) ; au survol, ses gestes : ouvrir (previsualiser),
//  le detail, les analyses precedentes, retirer de la discussion (une piece
//  retiree se remet). Une recherche parmi les fichiers quand il y en a trois ou
//  plus. L'ouvrir ou le fermer ne touche pas a la conversation.
//
//  Il ne connait ni l'Assistant ni le panneau : il recoit le contexte (setContext,
//  a chaque changement de la conversation - attach::buildContext) et rend chaque
//  geste par un rappel ; le panneau decide (confirmer un retrait, ouvrir la
//  visionneuse, montrer le detail). Les chaines sont en francais, l'aspect celui
//  du theme sombre industriel (les puces d'etat de la bande du plan).
//
//  INTEGRATION (AiPanel) : comme la bande du plan - un enfant du panneau, place
//  au-dessus de la discussion, de la hauteur heightFor(largeur, max) (repliee :
//  28 px ; 0 jamais : le compteur reste visible), invalidateLayout() du parent
//  quand il s'ouvre ou se ferme (le rappel `toggled`).
// =============================================================================
#pragma once

#include "AiAttachments.hpp"
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
class InputText;
}

namespace app::ai {

class AiContextPane final : public ui::Widget {
public:
    struct Callbacks {
        std::function<void(const std::string& attachmentId)> open;       // ouvrir, previsualiser
        std::function<void(const std::string& attachmentId)> details;    // les informations detaillees
        std::function<void(const std::string& attachmentId)> analyses;   // les analyses precedentes
        std::function<void(const std::string& attachmentId)> remove;     // retirer de la discussion (le panneau confirme)
        std::function<void(const std::string& attachmentId)> restore;    // remettre une piece retiree
        std::function<void(const std::string& query)>        search;     // la recherche a change (le volet filtre deja)
        std::function<void(bool expanded)>                   toggled;    // ouvert ou ferme : refaire la mise en page
    };
    enum class Action : std::uint8_t { Open, Details, Analyses, Remove };

    AiContextPane(std::string id, Callbacks callbacks);

    // Le contexte a montrer (le fil de l'interface ; a chaque changement de la conversation ou de ses pieces).
    void setContext(attach::DiscussionContext ctx);
    [[nodiscard]] const attach::DiscussionContext& context() const noexcept { return ctx_; }

    // La hauteur voulue a cette largeur, au plus `maxH` (repliee : la ligne du haut).
    [[nodiscard]] float heightFor(float width, float maxH) const;
    [[nodiscard]] bool  expanded() const noexcept { return open_; }
    void                setExpanded(bool open);
    void                setSearch(std::string text);
    [[nodiscard]] const std::string& search() const noexcept { return search_; }
    // Les pieces retirees : montrees (grisees, avec << Remettre >>) ou non.
    void setShowRemoved(bool on);
    [[nodiscard]] bool showRemoved() const noexcept { return showRemoved_; }

    // Ce que le volet montre (les essais) : les fichiers listes, puis ses lignes de texte.
    [[nodiscard]] std::vector<const attach::ContextFile*> shown() const;
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] std::string counterText() const;   // "Fichiers joints : 4" (et les manquants)
    [[nodiscard]] gfx::Rect headRect() const;
    // La ligne du fichier montre `index` (vide : hors de la vue), le bouton d'un geste sur elle.
    [[nodiscard]] gfx::Rect rowRect(std::size_t index) const;
    [[nodiscard]] gfx::Rect actionRect(std::size_t index, Action a) const;

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    [[nodiscard]] float       listTop() const;
    [[nodiscard]] std::size_t visibleRows() const;
    [[nodiscard]] bool        searchVisible() const;
    void                      fire(const std::function<void(const std::string&)>& f, std::size_t index) const;

    Callbacks                 cb_;
    attach::DiscussionContext ctx_;
    bool                      open_{false};
    bool                      showRemoved_{false};
    std::string               search_;
    float                     scroll_{0.f};
    int                       hover_{-1};
    ui::InputText*            searchBox_{nullptr};
    core::ConnectionScope     links_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

} // namespace app::ai
