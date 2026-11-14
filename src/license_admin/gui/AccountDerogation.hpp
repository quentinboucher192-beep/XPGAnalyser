// =============================================================================
//  license_admin/gui/AccountDerogation.hpp - 1.52.0 : MON COMPTE, << DEROGATION
//  RECUE >> (un admin ; docs/ADMINS.md, par. 11 ; l'agent D)
// -----------------------------------------------------------------------------
//  La derogation de l'admin+ pour CE poste (sa cle d'admin), telle que le regard
//  des 15 s l'a relue (Workspace::derogation) :
//    - EN ATTENTE DU CODE : << L'admin+ t'a envoye une derogation (4 h) : tape le
//      code qu'il voit sur son PC. >>, l'heure limite, les essais qui restent ; le
//      champ (les espaces, les tirets acceptes) et << Activer >>
//      (Workspace::activateDerogation, par runSlow) ; le refus du service dit ici ;
//    - EN COURS : << Tu vois tout comme l'admin+ : il reste 3 h 52 (jusqu'a 19:42). >>
//  Le code tape n'est jamais garde : le champ se vide a chaque reponse. Le meme
//  geste que le dialogue du code (DerogationDialog.cpp, makeDerogationCodeDialog).
//  L'implementation : DerogationDialog.cpp.
// =============================================================================
#pragma once

#include "Page.hpp"

#include "../../core/Signal.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>

namespace license_admin::gui {

class DerogationCard final : public ui::Widget {
public:
    explicit DerogationCard(ShellHost host);
    ~DerogationCard() override;

    void refresh();
    // A montrer (un admin : sa derogation en attente du code, ou en cours) ; sa hauteur.
    [[nodiscard]] bool wanted() const;
    [[nodiscard]] float wantedHeight(float width) const;
    // Les essais : la phrase du dernier refus (ou de la reussite).
    [[nodiscard]] const std::string& said() const noexcept { return said_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    [[nodiscard]] bool pending() const;
    [[nodiscard]] bool active() const;
    void activate();

    ShellHost              host_;
    ui::InputText*         code_{nullptr};
    ui::Button*            activate_{nullptr};
    std::string            said_;
    bool                   ok_{false}, running_{false};
    std::shared_ptr<bool>  alive_ = std::make_shared<bool>(true);
    core::ConnectionScope  links_;
};

} // namespace license_admin::gui
