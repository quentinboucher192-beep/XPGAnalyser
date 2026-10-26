// =============================================================================
//  app/hmi/HmiDebugPane.hpp - 1.13.0 (lot J2) : Simulation > Debogage (l'IHM)
// -----------------------------------------------------------------------------
//  Le debogueur des scripts IHM, sur un seul ecran (le sous-onglet de la
//  simulation de l'IHM ; IHM Studio n'a pas d'automate a cote) :
//
//   * LA BARRE : Continuer (F5), Suivant (F10), Entrer (F11), Sortir (Maj+F11),
//     Pause ; et sous elle L'ETAT EN CLAIR (« En pause au point 2 : script
//     Statistiques_Pression, ligne 14, cycle IHM 1 243 - P = 3.2 »).
//   * A GAUCHE : LES POINTS D'ARRET (la case : actif ; ou ; la condition,
//     modifiable sur place ; les passages ; ce que le moteur en dit) et le champ
//     « + Point d'arret » (Statistiques_Pression 14 si P < 5) ; LES ESPIONS (une
//     expression, sa valeur maintenant - en pause, dans la portee de la ligne).
//   * AU CENTRE : LA PILE (cycle IHM > script > fonction...), chaque niveau
//     cliquable, et LE CODE du niveau choisi (lecture seule, la ligne surlignee,
//     les valeurs de la ligne a droite ; sa marge pose des points aussi).
//   * A DROITE : LES LOCALES du niveau choisi (VAR, VAR_TEMP, parametres,
//     compteurs de boucle), qui se deplient (structures, tableaux, listes).
//   * ETROIT (le panneau de droite de la simulation, ~570 px) : en lignes - la
//     pile et le code, les locales sur toute la largeur, puis les points et les
//     espions cote a cote ; l'etat en clair sur deux lignes.
//
//  Les points sont a la seance (HmiDebugSession) : fermer l'onglet ne les
//  enleve pas. Le moteur est celui de la simulation (hmi::Runtime).
// =============================================================================
#pragma once

#include "HmiDebugSession.hpp"
#include "HmiPanels.hpp"
#include "../../hmi/HmiCommands.hpp"     // hmi::DocumentPtr
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace app {

class HmiDebugPane final : public ui::Widget {
public:
    enum Action : int { AContinue = 1, AStepOver, AStepInto, AStepOut, APause, AClearBreakpoints, ADisableAll, ARemoveBreakpoint };
    HmiDebugPane(std::string id, hmi::DocumentPtr doc, std::function<hmi::Runtime*()> runtime);
    ~HmiDebugPane() override;

    // A chaque image (la simulation l'appelle) : rien ne se refait si rien n'a change ; les espions, quatre
    // fois par seconde en marche.
    void refresh(double now = 0.0);
    void runAction(int action);
    // F5, F10, F11, Maj+F11 en pause : la touche du debogueur (vrai : prise).
    bool debugKey(ui::Key key, bool shift);

    // ---- pour les scripts et les essais (sans la souris) ----
    // Un point tape : "Statistiques_Pression 14", "Statistiques_Pression:14", "Vue_A.OnCycle 3",
    // "Moyenne_Pression 5 si A > 3.0" (une fonction IHM). Faux et `why` : code ou ligne introuvable.
    bool addBreakpointText(const std::string& text, std::string* why = nullptr);
    bool addWatch(const std::string& expression);
    bool removeWatch(const std::string& expression);
    [[nodiscard]] std::vector<std::string> watchLines() const;          // "P * 2 = 6.4"
    [[nodiscard]] const std::string& stateLine() const noexcept { return state_; }
    [[nodiscard]] std::vector<std::string> stackLabels() const;         // la ligne d'arret d'abord
    [[nodiscard]] std::vector<std::string> localLines() const;          // "P : REAL = 3.2"
    [[nodiscard]] std::vector<std::string> breakpointLines() const;     // "Statistiques_Pression : 14 si P < 5 - 2 passages"
    bool chooseLevel(std::size_t level);                                 // 0 = la ligne d'arret
    [[nodiscard]] ui::MultiLineText& code() noexcept { return *code_; }
    [[nodiscard]] ui::TableView& breakpointTable() noexcept { return *bpTable_; }
    [[nodiscard]] ui::TableView& stackTable() noexcept { return *stackTable_; }
    [[nodiscard]] ui::TableView& localsTable() noexcept { return *localsTable_; }
    [[nodiscard]] ui::TableView& watchTable() noexcept { return *watchTable_; }
    [[nodiscard]] ui::InputText& breakpointField() noexcept { return *bpField_; }
    [[nodiscard]] ui::InputText& watchField() noexcept { return *watchField_; }
    [[nodiscard]] gfx::Rect buttonRect(int action) const;
    [[nodiscard]] std::string buttonText(int action) const;            // 1.13.0 : "Pause", "Annuler la pause"
    // Aller au code (le double clic sur un point, un niveau) : l'hote ouvre le script ou la fonction a sa ligne.
    std::function<void(hmi::Id script, hmi::Id function, int line)> goTo;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    class BreakModel;
    class StackModel;
    class LocalsModel;
    class WatchModel;
    friend class BreakModel;
    friend class WatchModel;
    struct LocalRow {
        std::string name, type, value, key;
        int         depth{0};
        bool        expandable{false}, open{false}, parameter{false};
    };
    [[nodiscard]] hmi::Runtime* runtime() const;
    [[nodiscard]] std::string codeName(hmi::Id script, hmi::Id function) const;
    [[nodiscard]] std::string codeText(hmi::Id script, hmi::Id function) const;
    void rebuild();
    void rebuildBreakpoints();
    void rebuildStack();
    void rebuildCode();
    void rebuildLocals();
    void readWatches();
    void say(const std::string& text, bool warning = false);
    [[nodiscard]] const hmi::DebugFrame* shownFrame() const;

    hmi::DocumentPtr                 doc_;
    std::function<hmi::Runtime*()>   runtimeOf_;
    std::vector<ui::Button*>         buttons_;           // dans l'ordre des Action (AContinue...)
    ui::Button*                      clearBtn_{nullptr};
    ui::Button*                      removeBtn_{nullptr};
    ui::Splitter*                    split_{nullptr};
    ui::Splitter*                    leftSplit_{nullptr};  // les points, les espions
    ui::Splitter*                    centerSplit_{nullptr}; // la pile, le code
    ui::TableView*                   bpTable_{nullptr};
    ui::TableView*                   watchTable_{nullptr};
    ui::TableView*                   stackTable_{nullptr};
    ui::TableView*                   localsTable_{nullptr};
    ui::MultiLineText*               code_{nullptr};
    HmiTitledPanel*                  codePanel_{nullptr};
    ui::InputText*                   bpField_{nullptr};
    ui::InputText*                   watchField_{nullptr};
    std::shared_ptr<BreakModel>      bpModel_;
    std::shared_ptr<StackModel>      stackModel_;
    std::shared_ptr<LocalsModel>     localsModel_;
    std::shared_ptr<WatchModel>      watchModel_;
    HmiBreakpointMargin              margin_;            // la marge du code montre : des points aussi
    HmiDebugTarget                   codeShown_{};
    std::string                      state_;
    std::string                      message_;
    bool                             messageWarn_{false};
    std::uint64_t                    messageRev_{0};     // la revision du moteur quand il a ete dit
    bool                             narrow_{false};     // le volet etroit (le panneau de droite) : en lignes
    std::string                      seen_;              // ce que l'ecran montre deja (une signature)
    double                           nextWatch_{0.0};
    std::vector<std::string>         watches_;
    std::vector<std::string>         watchValues_;
    std::vector<LocalRow>            locals_;
    std::set<std::string>            expanded_;          // les locales depliees (leur chemin)
    std::vector<std::uint32_t>       bpIds_;             // les points, dans l'ordre du tableau
    gfx::Rect                        band_{}, stateBand_{};
    core::ConnectionScope            links_;
};

} // namespace app
