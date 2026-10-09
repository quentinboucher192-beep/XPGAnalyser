// =============================================================================
//  app/hmi/HmiAskDialog.hpp - un dialogue qui demande avant d'agir (lot 17)
// -----------------------------------------------------------------------------
//  Supprimer un equipement (ses variables IHM a cocher, decochees d'office, et
//  son jumeau), le deplacer sur un autre port (changer son adresse, ou garder
//  la sienne), detecter ses zones (en direct, puis Appliquer) : un paragraphe,
//  une liste de cases, d'autres cases, des choix exclusifs (chacun peut porter
//  un champ), une note ; le bouton dit ce qui va se passer ("Supprimer
//  l'equipement et 1 variable").
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/PathBrowse.hpp"   // le bouton ... d'un champ de chemin

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiAskDialog final : public menu::WidgetMenu {
public:
    struct Item {
        std::string label, detail;
        bool        checked{false};
    };
    struct Option {
        std::string label, detail;
        std::string field;                  // un champ sous le choix : sa valeur de depart
        bool        hasField{false};
        std::string placeholder;
        // Un champ de CHEMIN : le bouton ... a sa droite ouvre l'explorateur de
        // fichiers (ui::openFile, ui::saveFile, ui::chooseFolder) ; le champ
        // prend alors toute la largeur.
        ui::PathBrowse browse{};
    };
    struct Spec {
        std::string         id{"dialog.hmiAsk"};
        std::string         title, text;     // text : le paragraphe du haut (\n : a la ligne)
        std::string         listTitle;       // au-dessus des cases
        std::vector<Item>   items;           // la liste (elle defile)
        std::vector<Item>   extras;          // d'autres cases, sous la liste
        std::vector<Option> options;         // des choix exclusifs
        int                 option{0};
        std::string         note;            // en bas, en gris
        std::string         confirm{"OK"}, cancel{"Annuler"};
        bool                danger{false};   // le bouton dit une suppression
        bool                plainItems{false};   // 1.11.22 : une liste a lire, sans cases (ce qui restera, par exemple)
        // Le libelle du bouton selon les cases et le choix (vide : confirm).
        std::function<std::string(const std::vector<bool>&, const std::vector<bool>&, int)> confirmLabel;
        // En direct : le paragraphe (remplace text), et si le bouton est permis.
        std::function<std::string()> live;
        std::function<bool()>        ready;
        // Un troisieme bouton (Arreter) ; vide : aucun.
        std::string                  stopLabel;
        std::function<void()>        onStop;
        float                        width{640.f}, height{0.f};   // 0 : selon le contenu
    };
    struct Answer {
        std::vector<bool> items, extras;
        int               option{0};
        std::string       field;
    };

    explicit HmiAskDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return spec_.title; }
    void Update(const menu::FrameContext& f) override;
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;

    // La reponse, dans DialogResult::payload.
    [[nodiscard]] static Answer parse(const std::string& payload);

protected:
    core::Status buildUi() override;

private:
    [[nodiscard]] std::string payload() const;
    void sync();
    void finish(bool ok);

    Spec                                          spec_;
    std::vector<ui::Checkbox*>                    items_, extras_;
    std::vector<ui::RadioButton*>                 radios_;
    std::vector<ui::InputText*>                   fields_;
    std::vector<ui::Widget*>                      browses_;     // le bouton ... de chaque champ (nul : aucun)
    std::shared_ptr<ui::RadioGroup>               group_;
    ui::Button*                                   ok_{nullptr};
    ui::Button*                                   stop_{nullptr};
    ui::Widget*                                   text_{nullptr};
    bool                                          done_{false};
    core::ConnectionScope                         links_;
};

} // namespace app
