// =============================================================================
//  app/ProgramExportDialog.hpp - 1.8.0 : la boite "Exporter le programme lisible"
// -----------------------------------------------------------------------------
//  QUOI : tout le programme, une unite de programme, les sections choisies (les
//  choix permis selon d'ou l'on vient). FORMATS : Excel, PDF, texte (plusieurs a
//  la fois). CONTENU : le guide et le sommaire, ce que chaque section lit et
//  ecrit, les numeros de ligne, le role (les icones), les parametres des
//  unites, les annexes (blocs DFB, variables globales). ENREGISTRER DANS : un
//  dossier (le bouton ... ouvre l'explorateur), les noms des fichiers qui vont
//  etre ecrits, "Ouvrir le dossier a la fin". Le bouton dit combien de fichiers.
//  L'export tourne ensuite en tache de fond (MainAnalysisScreen).
// =============================================================================
#pragma once

#include "../core/Signal.hpp"
#include "../menu/IMenu.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace ui {
class Checkbox;
class RadioButton;
class RadioGroup;
class InputText;
class Button;
}

namespace app {

class ProgramExportDialog final : public menu::WidgetMenu {
public:
    struct Scope {
        std::string label, detail;
        std::string stem;              // le nom des fichiers pour ce choix (sans extension)
    };
    struct Spec {
        std::vector<Scope>        scopes;
        int                       scope{0};
        std::array<bool, 3>       formats{true, true, true};       // Excel, PDF, texte
        // guide, lit/ecrit, numeros, role, parametres, DFB, variables
        std::array<bool, 7>       content{true, true, true, true, true, true, true};
        std::string               dfbDetail, variablesDetail;      // "8 blocs, 1 675 lignes"
        std::string               folder;
        bool                      openFolder{true};
    };
    struct Answer {
        int                 scope{0};
        std::array<bool, 3> formats{};
        std::array<bool, 7> content{};
        std::string         folder;
        bool                openFolder{true};
    };

    explicit ProgramExportDialog(Spec spec);
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return "Exporter le programme lisible"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    void Update(const menu::FrameContext& f) override;

    [[nodiscard]] static Answer parse(const std::string& payload);

    // Pour les scripts et les tests.
    void setFormat(int i, bool on);
    void setScope(int i);
    void setFolder(const std::string& folder);
    void accept() { finish(true); }

protected:
    core::Status buildUi() override;

private:
    friend class ExportBody;
    [[nodiscard]] std::string payload() const;
    [[nodiscard]] int formatCount() const;
    void sync();
    void finish(bool ok);

    Spec                               spec_;
    std::vector<ui::RadioButton*>      radios_;
    std::shared_ptr<ui::RadioGroup>    group_;
    std::array<ui::Checkbox*, 3>       formats_{};
    std::array<ui::Checkbox*, 7>       content_{};
    ui::Checkbox*                      open_{nullptr};
    ui::InputText*                     folder_{nullptr};
    ui::Button*                        ok_{nullptr};
    bool                               done_{false};
    core::ConnectionScope              links_;
};

} // namespace app
