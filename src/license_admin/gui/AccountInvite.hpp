// =============================================================================
//  license_admin/gui/AccountInvite.hpp - 1.52.0 : MON COMPTE, << TON CODE
//  D'INVITATION >> ET << TON AJOUT >> (XPG Admin ; docs/ADMINS.md, par. 4 et 12)
// -----------------------------------------------------------------------------
//  XPG Admin sans derogation n'a plus Administrateurs (ClassicView.hpp) : la carte
//  du code d'invitation y etait (1.51.0) et vient dans Mon compte, la meme :
//    - PAS ENCORE ADMIN (aucune cle), ou une cle sans inscription connue (le
//      service n'en a pas, un code refuse) : le code de l'installateur (dit), sinon
//      le champ ; << Devenir admin >> (Workspace::joinWithInvitation, par runSlow) ;
//      un code refuse (ici, ou au premier lancement : Shell::autoJoin le dit par
//      requestPageFocus("compte.invitation", "code-refuse:<la phrase>")) : la
//      phrase du service en rouge, et le code se retape ; sans adresse du service :
//      << Regler le service... >> (Mon compte > Ce PC) ;
//    - AJOUT EN COURS (le code accepte) : l'Admin+ signe la cle d'office ; << Verifier
//      maintenant >> (la coquille regarde deja toutes les 15 s).
//  L'Admin+ (l'edition Plus) ne la montre jamais.
// =============================================================================
#pragma once

#include "Page.hpp"

#include "../../core/Signal.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <memory>
#include <string>

namespace license_admin::gui {

class InviteCard final : public ui::Widget {
public:
    explicit InviteCard(ShellHost host);
    ~InviteCard() override;

    // Le role relu ; la phrase d'un code refuse au premier lancement (Shell::autoJoin).
    void refresh();
    // A montrer (XPG Admin : pas encore admin, une cle sans inscription, ou en cours d'ajout) ; sa hauteur a cette largeur.
    [[nodiscard]] bool wanted() const;
    [[nodiscard]] float wantedHeight(float width) const;
    // Les essais : ce que la carte dit (la phrase d'un code refuse, l'etat du dernier regard).
    [[nodiscard]] const std::string& problem() const noexcept { return problem_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    enum class Mode { Hidden, Code, Waiting };
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    void join();
    void check();
    void sync();

    ShellHost              host_;
    ui::InputText*         code_{nullptr};
    ui::Button*            join_{nullptr};
    ui::Button*            service_{nullptr};
    ui::Button*            check_{nullptr};
    std::string            fileCode_, problem_, state_;   // le code de l'installateur ; la phrase d'un code refuse ; le dernier etat lu
    Seconds                lastCheck_{0};
    bool                   checking_{false};
    std::shared_ptr<bool>  alive_ = std::make_shared<bool>(true);
    core::ConnectionScope  links_;
};

} // namespace license_admin::gui
