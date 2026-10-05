#pragma once
// 1.11 (T1, tranche 15) : les chemins des « A toi » lus dans la page des
// expressions (HmiExprPageView, T3), pour la scene (TutorialApp.cpp, read) :
//
//   essai.convient : "oui" quand le champ d'essai dit « convient a une case
//                    <type> », "non" sinon, ou sans page a l'ecran ;
//   essai.texte    : le texte du champ d'essai tel qu'un @verifier le relit
//                    (trialText) ; "" sans page a l'ecran. (Decision du chef,
//                    03/10 : l'A toi de l'etape 3 des expr-* verifie ce que
//                    l'utilisateur a ecrit, car choisir le type remplit deja le
//                    champ avec un exemple qui convient.)
//
// essai.texte suit UNE regle, celle de la page (HmiExprPageView::trialText, T3) :
// les espaces des deux bouts retires, chaque suite d'espaces reduite a un seul,
// une chaine '...' ou "..." gardee telle quelle ($' et $" compris).
// Integration I111 (tranche 8) : la copie de cette regle qui etait ici (T1,
// tranche 15, faite avant que la page l'expose) est retiree ; la scene lit
// page->trialText().
//
// La page lue est la premiere page VISIBLE sous l'ecran du dessus. Sans fichier
// .cpp : hmi_editor_test (tutoriels111) le joue sur une vraie page, sans ecran.

#include "../hmi/HmiExprPageView.hpp"
#include "../../ui/Widget.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace app::tutorials {

// La premiere page des expressions visible sous `top` ; nullptr : aucune.
inline HmiExprPageView* visibleExprPage(ui::Widget* top) {
    HmiExprPageView* found = nullptr;
    auto find = [&found](auto&& self, ui::Widget& w) -> void {
        if (found || !w.visible()) return;
        if (auto* page = dynamic_cast<HmiExprPageView*>(&w)) {
            found = page;
            return;
        }
        for (const auto& c : w.children()) self(self, *c);
    };
    if (top) find(find, *top);
    return found;
}

// essai.convient et essai.texte ; nullopt : un autre chemin (la scene le lit ailleurs).
inline std::optional<std::string> readExprTrial(ui::Widget* top, std::string_view path) {
    if (path != "essai.convient" && path != "essai.texte") return std::nullopt;
    auto* page = visibleExprPage(top);
    if (path == "essai.convient")
        return std::string(page && page->outcome().ok && !page->outcome().fits.empty() ? "oui" : "non");
    return page ? page->trialText() : std::string();
}

} // namespace app::tutorials
