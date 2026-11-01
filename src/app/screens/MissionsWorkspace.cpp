// =============================================================================
//  app/screens/MissionsWorkspace.cpp - 1.22.0 : le tableau de bord d'une mission IA, au centre
// -----------------------------------------------------------------------------
//  Le panneau de l'Assistant (AiPanel) ne fait aucun geste sur l'ecran : son
//  hote UiHost::openMission (AiWorkspace.cpp) arrive ici. Une mission = un
//  onglet du centre ("Mission IA . Station de pompage", l'icone des taches), une
//  page ai::AiMissionBoard retenue dans hmiTabs_ sous "mission:<identifiant>",
//  ouverte ou montree (en onglet, ou sa fenetre detachee devant) comme
//  << Chercher des fichiers >>. Le tick (tickAssistant) suit chaque tableau de
//  bord ouvert : il relit sa mission quand la revision des missions change, son
//  titre suit un renommage, sa diode dit que l'IA travaille pour elle.
// =============================================================================
#include "Screens.hpp"

#include "../App.hpp"
#include "../ai/AiMissionBoard.hpp"
#include "../ai/AiPanel.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>

namespace app {

using ui::Checkbox;
using ui::Icon;
using ui::TabControl;

namespace {
std::string missionKey(const std::string& id) { return "mission:" + id; }
constexpr std::string_view kMissionPrefix = "mission:";
} // namespace

ai::AiMissionBoard* MainAnalysisScreen::missionBoard(const std::string& missionId) const {
    const auto it = hmiTabs_.find(missionKey(missionId));
    return it == hmiTabs_.end() ? nullptr : dynamic_cast<ai::AiMissionBoard*>(it->second);
}

bool MainAnalysisScreen::openMissionBoard(const std::string& missionId, int mode) {
    if (!centre_ || !aiPanel_) return false;
    aiPanel_->ensureLoaded();   // les missions sont relues avec les conversations
    if (!aiPanel_->assistant().findMission(missionId)) return false;
    // Le centre doit etre a l'ecran (comme Affichage > Panneaux a afficher).
    if (panels_.size() > 2 && !panels_[2].box->isChecked()) panels_[2].box->setState(Checkbox::State::Checked);
    const auto show = [mode](ai::AiMissionBoard& board) {
        board.setMode(mode == 1 ? ai::AiMissionBoard::Mode::Resume : ai::AiMissionBoard::Mode::Board);
        if (mode == 2) board.showHistory();
    };
    if (auto* board = missionBoard(missionId)) {
        show(*board);
        if (showPage(board)) return true;   // en onglet, ou sa fenetre detachee devant
    }
    auto made = std::make_unique<ai::AiMissionBoard>("mission." + missionId, *aiPanel_, missionId);
    auto* board = made.get();
    const auto tab = centre_->addTab(TabControl::Tab{board->tabTitle(), Icon::Task, /*closable=*/true, false}, std::move(made));
    hmiTabs_[missionKey(missionId)] = board;
    centre_->setCurrentIndex(tab);
    show(*board);
    return true;
}

void MainAnalysisScreen::tickMissionBoards() {
    if (!centre_ || !aiPanel_) return;
    for (const auto& [key, page] : hmiTabs_) {
        if (key.compare(0, kMissionPrefix.size(), kMissionPrefix) != 0) continue;
        auto* board = dynamic_cast<ai::AiMissionBoard*>(page);
        if (!board) continue;
        const bool changed = board->poll();
        const int index = centre_->indexOf(board);   // detachee : pas d'onglet a mettre a jour
        if (index < 0) continue;
        const auto at = static_cast<std::size_t>(index);
        if (changed)
            if (const auto* t = centre_->tab(at); t && t->title != board->tabTitle()) centre_->setTabTitle(at, board->tabTitle());
        if (const auto* t = centre_->tab(at); t && t->live != board->busy()) centre_->setTabLive(at, board->busy());
    }
}

} // namespace app
