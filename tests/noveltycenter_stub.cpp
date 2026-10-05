// =============================================================================
//  tests/noveltycenter_stub.cpp - 1.11 (I111, decision 16 du 03/10)
// -----------------------------------------------------------------------------
//  Le centre des nouveautes pour les essais qui compilent TopBar.cpp sans
//  l'appli (api2_test, topbar8_test, lot8tutorials_test). TopBar.cpp ne lui
//  demande que marksHidden() (la pastille du menu Aide) ; le vrai
//  NoveltyCenter.cpp tire l'appli entiere (HelpCenterScreen, MainAnalysisScreen,
//  Settings...). Les deux definitions sont celles de NoveltyCenter.cpp : un
//  centre par processus, et les reperes masques selon l'etat du registre
//  (help::news, dans xpg_help). Sans elles, ces essais ne se liaient plus.
// =============================================================================
#include "../src/app/NoveltyCenter.hpp"
#include "../src/help/Novelties.hpp"

namespace app {

NoveltyCenter& noveltyCenter() {
    static NoveltyCenter c;
    return c;
}

bool NoveltyCenter::marksHidden() const { return help::news::session().marksHidden; }

} // namespace app
