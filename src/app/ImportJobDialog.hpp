// =============================================================================
//  app/ImportJobDialog.hpp - 1.8.0 : la fenetre qui suit un import
// -----------------------------------------------------------------------------
//  Modale, mais l'application continue a se dessiner (updatesBelow) : l'import
//  tourne a cote (ImportJob). La barre et son pourcentage, l'etape en cours et
//  son detail ("Sections : 41 - SFC_PurgeB"), chaque etape (faite, sa duree ;
//  en cours ; a venir), les compteurs (sections, unites, blocs, types,
//  variables ; ou racks, modules, voies), le temps ecoule.
//  "Continuer en arriere-plan" ferme la fenetre : la barre du haut (Taches de
//  fond) suit l'import, la cloche previent a la fin. "Annuler" : rien n'a change
//  dans le projet. Fini : "Termine en 0,8 s", la fenetre se ferme seule une
//  seconde apres (le recapitulatif d'un .XPG suit). Un echec : la raison, et
//  Fermer.
//
//  La reponse (DialogResult::payload) : "done", "background", "cancelled",
//  "failed".
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"
#include "ImportJob.hpp"

#include <memory>
#include <string>

namespace ui {
class Button;
}

namespace app {

class ImportJobDialog final : public menu::WidgetMenu {
public:
    explicit ImportJobDialog(std::shared_ptr<ImportJob> job, std::string where);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override;
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    void background();         // Continuer en arriere-plan
    void cancelImport();       // Annuler

protected:
    core::Status buildUi() override;

private:
    friend class ProgressBody;
    void finish(const std::string& payload);

    std::shared_ptr<ImportJob> job_;
    std::string                where_;         // "dans Projet"
    ImportJob::State           state_;
    ui::Button*                bg_{nullptr};
    ui::Button*                cancel_{nullptr};
    double                     doneAt_{-1.0};  // quand la fin a ete vue (la fenetre se ferme apres)
    double                     now_{0.0};
    bool                       closed_{false};
    core::ConnectionScope      links_;
};

} // namespace app
