// =============================================================================
//  app/ai/AiMissionsView.hpp - 1.22.0 : L'ONGLET << MISSIONS IA >> DU PANNEAU
// -----------------------------------------------------------------------------
//  Le troisieme onglet du panneau de l'Assistant (Conversation | Notifications |
//  Missions IA) : les missions IA persistantes (IA/missions), a la place de la
//  conversation quand il est choisi. De haut en bas :
//
//    la barre     la recherche (le nom, la demande, les etapes, les elements
//                 crees - mission::search, les plus pertinentes d'abord) et +
//                 (Nouvelle mission IA...) ;
//    les filtres  Toutes | En cours | Archivees (leurs nombres) ;
//    la creation  (le +) : le texte de la demande, << Creer la mission >>
//                 (Assistant::createMission : l'IA analyse le projet et propose
//                 un plan ; rien n'est modifie) ;
//    une ligne    le nom, l'etat (sa couleur), le projet, la derniere activite,
//                 la barre de la progression globale (mission::progressOf),
//                 1.22.1 : l'etape du moment, sa progression reelle et ce
//                 qu'elle fait maintenant (la ligne choisie : toutes ses
//                 etapes), la prochaine action, les erreurs ; ses boutons :
//                 << Tableau de bord >> (au centre) et << Reprendre >> (l'ecran
//                 de reprise) ou << Pause >>.
//
//  Le clic droit : Ouvrir le tableau de bord, l'ecran de reprise, la
//  conversation, Proposer la suite, Mettre en pause / Reprendre, Renommer...,
//  l'historique, Archiver / Desarchiver, Annuler la mission..., 1.22.3 : Forcer la
//  fin de la mission... (grise, et pourquoi, si elle est finie). Le clavier :
//  Haut, Bas, Debut, Fin, Page precedente / suivante, Entree (le tableau de
//  bord), F2 (renommer), Suppr (archiver), la touche Menu.
//
//  Les gestes passent par le panneau (AiPanel::openMission, missionAct...). Rien
//  de lourd au dessin : les lignes sont refaites quand la revision des missions,
//  le filtre, la recherche ou la largeur change.
// =============================================================================
#pragma once

#include "AiMissionBoard.hpp"   // missionui
#include "../../core/Signal.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace app::ai {

class AiPanel;
class AiPromptEdit;

class AiMissionsView final : public ui::Widget {
public:
    enum class Filter : std::uint8_t { All, Open, Archived };   // Toutes (non archivees) | En cours | Archivees

    AiMissionsView(std::string id, AiPanel& panel);

    // Relit les missions si leur revision a change (le panneau, a chaque image) ; vrai : la liste a change.
    bool poll();
    void refresh();

    void                 setFilter(Filter f);
    [[nodiscard]] Filter filter() const noexcept { return filter_; }
    [[nodiscard]] std::size_t count(Filter f) const noexcept;
    // La recherche (le champ la pose a chaque frappe) : le nom, la demande, les etapes, les elements crees.
    void                             setSearch(std::string query);
    [[nodiscard]] const std::string& search() const noexcept { return query_; }
    [[nodiscard]] ui::InputText&     searchField() noexcept { return *search_; }

    // ---- la creation (le +) ----
    void                        openComposer(bool open);
    [[nodiscard]] bool          composerOpen() const noexcept { return composing_; }
    [[nodiscard]] AiPromptEdit& composer() noexcept { return *composer_; }
    [[nodiscard]] ui::Button&   newButton() noexcept { return *new_; }
    [[nodiscard]] ui::Button&   createButton() noexcept { return *create_; }
    [[nodiscard]] ui::Button&   cancelButton() noexcept { return *cancel_; }
    bool                        create();   // le texte de la demande -> la mission

    // ---- les lignes montrees (apres le filtre et la recherche) ----
    [[nodiscard]] std::size_t        rowCount() const noexcept { return rows_.size(); }
    [[nodiscard]] std::string        rowId(std::size_t row) const;
    // "Station de pompage | En cours | Station | 3/9 etapes . 38 % | Prochaine : ... | erreurs" (les essais).
    [[nodiscard]] std::string        rowText(std::size_t row) const;
    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] int                selectedRow() const noexcept { return selected_; }
    bool                             select(int row);
    bool                             selectMission(const std::string& id);   // la ligne de cette mission (le filtre change au besoin)
    bool                             open(int row);                          // le tableau de bord (au centre)
    bool                             second(int row);                        // le second bouton : Reprendre (l'ecran de reprise) / Pause
    [[nodiscard]] std::string        secondLabel(std::size_t row) const;     // "Reprendre", "Pause" ou "" (aucun)

    // ---- la geometrie (apres une mise en page) ----
    [[nodiscard]] gfx::Rect rowRect(std::size_t row) const;                  // vide : hors de la vue
    [[nodiscard]] gfx::Rect buttonRect(std::size_t row, int which) const;    // 0 : Tableau de bord, 1 : le second
    [[nodiscard]] gfx::Rect filterRect(Filter f) const noexcept;

    // ---- le clic droit ----
    [[nodiscard]] ui::PopupMenu* menu() const noexcept { return menu_; }
    void openRowMenu(int row, gfx::Point at);
    enum MenuId : int {
        kMenuOpen = 1, kMenuResumeScreen, kMenuConversation, kMenuContinue, kMenuPause, kMenuResume, kMenuRename, kMenuHistory,
        kMenuArchive, kMenuUnarchive, kMenuCancel,
        kMenuForceFinish   // 1.22.3 : Forcer la fin de la mission... (une question d'abord)
    };
    void menuChosen(int id);

    // ---- renommer (F2, le clic droit) ----
    void                         startRename(int row);
    bool                         commitRename(const std::string& name);
    void                         cancelRename();
    [[nodiscard]] bool           renaming() const noexcept { return !renameId_.empty(); }
    [[nodiscard]] ui::InputText& renameField() noexcept { return *rename_; }

    // La pastille de l'onglet : les missions qui attendent une decision (en pause, bloquees, une proposition a valider,
    // une etape en erreur, un appel interrompu) ; alerte : une erreur ou un blocage.
    [[nodiscard]] std::size_t attentionCount() const noexcept { return attention_; }
    [[nodiscard]] bool        attentionAlert() const noexcept { return alert_; }

    [[nodiscard]] std::string liveTooltip(gfx::Point mouse) const override;
    [[nodiscard]] bool        hasTooltip() const override { return true; }

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    // 1.22.1 : une etape en une ligne - sa progression reelle (la petite barre) et ce qu'elle fait maintenant.
    struct StepLine {
        std::string     text, tip;
        missionui::Tone tone{missionui::Tone::Muted};
        double          frac{0.0};
        bool            bar{false}, live{false};
    };
    struct Row {
        std::string                 id, name, state, meta, count, next, problems, second, tip;
        missionui::Tone             stateTone{missionui::Tone::Muted}, problemsTone{missionui::Tone::Error};
        double                      frac{0.0};
        bool                        archived{false}, over{false}, paused{false}, canPause{false}, canContinue{false};
        std::string                 continueWhy;
        StepLine                    moment;          // 1.22.1 : l'etape du moment (missionui::momentOf) ; vide : rien en cours
        std::vector<StepLine>       steps;           // 1.22.1 : la ligne choisie - toutes ses etapes (les premieres)
        std::size_t                 moreSteps{0};
        float                       y{0.f}, h{0.f};
    };
    void rebuild();        // relit les missions : les lignes (le filtre, la recherche)
    void fillSteps(Row& r) const;                   // 1.22.1 : les etapes de la ligne choisie
    [[nodiscard]] std::size_t extraLines(const Row& r, bool selected) const noexcept;
    [[nodiscard]] std::int64_t liveKeyAll() const;   // 1.22.1 : le direct de toutes les missions (0 : rien ne travaille)
    void layoutRows();     // leurs hauteurs et leurs places
    void ensureVisible(int row);
    void say(const std::string& text) const;
    [[nodiscard]] int       rowAt(gfx::Point p) const;
    [[nodiscard]] gfx::Rect rowArea(const Row& r) const;   // a l'ecran (le defilement compris)
    [[nodiscard]] gfx::Rect buttonArea(const Row& r, int which) const;
    [[nodiscard]] float     viewH() const noexcept { return list_.h; }

    AiPanel&                  panel_;
    std::vector<Row>          rows_;
    std::uint64_t             seen_{0};
    std::int64_t              liveKey_{0};   // 1.22.1 : le direct (l'appel a l'IA d'une mission, une application de fond)
    bool                      generating_{false};
    Filter                    filter_{Filter::All};
    std::string               query_;
    std::size_t               counts_[3]{0, 0, 0};
    std::size_t               attention_{0};
    bool                      alert_{false};
    int                       selected_{-1};
    std::string               selectedId_;
    int                       hover_{-1}, hoverButton_{-1};
    int                       menuRow_{-1};
    std::string               menuId_;
    std::string               renameId_;
    bool                      composing_{false};
    float                     scroll_{0.f}, content_{0.f}, laidW_{-1.f};
    gfx::Rect                 bar_{}, chips_{}, compose_{}, list_{}, chipAll_{}, chipOpen_{}, chipArchived_{};
    ui::InputText*            search_{nullptr};
    ui::Button*               new_{nullptr};
    AiPromptEdit*             composer_{nullptr};
    ui::Button*               create_{nullptr};
    ui::Button*               cancel_{nullptr};
    ui::InputText*            rename_{nullptr};
    ui::PopupMenu*            menu_{nullptr};
    ui::EdgeScrollBar         listBar_;
    core::ConnectionScope     links_;
    mutable std::vector<std::pair<gfx::Rect, std::string>> tips_;
};

} // namespace app::ai
