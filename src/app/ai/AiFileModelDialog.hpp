// =============================================================================
//  app/ai/AiFileModelDialog.hpp - 1.21.0 : LA FENETRE << MODELE... >> D'UN CLASSEUR JOINT
// -----------------------------------------------------------------------------
//  Ouverte par le geste << Modele... >> de la ligne d'un classeur (.xlsx, .xlsm)
//  dans << Contexte de la discussion >> (AiContextPane). Une fenetre du logiciel
//  comme les autres : elle se deplace par son titre, s'agrandit par un bord,
//  Echap la ferme.
//
//    - en haut, ce que l'analyse a trouve : << Modele reconnu : Liste E/S
//      armoire 3 >>, ou le genre detecte et sa confiance (<< Liste
//      d'entrees/sorties - detectee, confiance 85 % >>), ou << Autre >> ;
//    - la feuille, le genre, la ligne d'en-tete, les lignes a sauter dessous ;
//    - LA CORRESPONDANCE DES COLONNES : un role par ligne (repere, designation,
//      type, adresse...), sa colonne choisie dans une liste (A - Repere...) ;
//    - L'APERCU : les premieres lignes lues avec cette correspondance, et le
//      nombre de lignes lues ;
//    - LES MODELES DE FICHIERS de la memoire : en CHOISIR un (il est cherche dans
//      ce classeur : sa feuille, sa ligne d'en-tete), ou ENREGISTRER COMME
//      MODELE (un nom) - un classeur aux memes intitules sera lu directement ;
//    - << Utiliser pour ce fichier >> garde le choix AVEC LA PIECE JOINTE (l'IA
//      integree, le contexte d'un fournisseur et l'outil des lignes s'en
//      servent) ; << Revenir a la detection >> l'oublie.
//
//  Le classeur est celui que l'Assistant a lu sur un fil (Assistant::workbookOf) ;
//  pas encore lu : la fenetre le dit et l'attend (Update). Les gestes existent
//  sans ecran (les essais).
// =============================================================================
#pragma once

#include "AiFileModels.hpp"
#include "../../core/Signal.hpp"
#include "../../menu/IMenu.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ui { class Button; class DropDown; class InputText; class TableView; }

namespace app::ai {

class Assistant;

class AiFileModelDialog final : public menu::WidgetMenu {
public:
    AiFileModelDialog(Assistant& assistant, std::string attachmentId, std::function<void(const std::string&)> say = {});
    ~AiFileModelDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Mod\xC3\xA8le de fichier"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;
    void                           Update(const menu::FrameContext& fc) override;

    // ---- l'etat et les gestes (l'ecran, les essais) ----------------------------------------------------------------
    [[nodiscard]] bool                         ready() const noexcept { return workbook_ != nullptr; }
    [[nodiscard]] const sheets::Layout&        layout() const noexcept { return layout_; }
    [[nodiscard]] const sheets::Reading&       preview() const noexcept { return preview_; }
    [[nodiscard]] std::size_t                  rowsRead() const noexcept { return rows_; }
    [[nodiscard]] std::string                  headline() const;   // ce que l'analyse a trouve
    [[nodiscard]] const std::string&           modelId() const noexcept { return modelId_; }
    [[nodiscard]] const std::string&           message() const noexcept { return message_; }
    [[nodiscard]] bool                         messageOk() const noexcept { return messageOk_; }
    [[nodiscard]] const std::vector<sheets::FileModel>& models() const noexcept { return models_; }
    bool setSheet(const std::string& name);
    bool setGenre(sheets::Genre g);
    bool setHeaderLine(std::size_t line);         // 1 : la premiere ligne
    bool setSkip(std::size_t n);                  // les lignes sautees sous l'en-tete
    bool setColumn(const std::string& role, int col);   // -1 : aucune
    bool applyModel(const std::string& idOrName); // un modele de la memoire, cherche dans ce classeur
    bool saveAsModel(const std::string& name);    // enregistre le modele, puis l'utilise pour ce fichier
    bool use();                                   // garde ce choix avec la piece jointe
    bool reset();                                 // revient a la reconnaissance de l'analyse
    void close();

protected:
    core::Status buildUi() override;

private:
    class Body;
    class MapModel;
    class PreviewModel;
    bool load();                      // le classeur lu (l'Assistant) et l'etat de depart
    void recompute();                 // la lecture, l'apercu, les tables, les champs
    void fillWidgets();
    void say(std::string text, bool ok);
    [[nodiscard]] std::vector<std::string> mapRoles() const;
    [[nodiscard]] std::vector<std::string> columnChoices() const;

    Assistant&                               assistant_;
    std::string                              id_, name_;
    std::function<void(const std::string&)>  sayOut_;
    std::shared_ptr<const sheets::Workbook>  workbook_;
    std::optional<sheets::Recognition>       recognition_;
    bool                                     chosenByUser_{false};
    sheets::Layout                           layout_;
    sheets::Layout                           modelLayout_;   // le modele applique tel quel (une retouche le quitte)
    std::string                              modelId_, modelName_;
    double                                   confidence_{0};
    sheets::Reading                          preview_;
    std::size_t                              rows_{0};
    std::vector<sheets::FileModel>           models_;
    std::string                              message_;
    bool                                     messageOk_{true};
    bool                                     syncing_{false};
    bool                                     done_{false};
    std::shared_ptr<MapModel>                mapModel_;
    std::shared_ptr<PreviewModel>            previewModel_;
    ui::DropDown*                            sheetBox_{nullptr};
    ui::DropDown*                            genreBox_{nullptr};
    ui::InputText*                           headerIn_{nullptr};
    ui::InputText*                           skipIn_{nullptr};
    ui::TableView*                           mapTable_{nullptr};
    ui::TableView*                           previewTable_{nullptr};
    ui::DropDown*                            modelBox_{nullptr};
    ui::Button*                              applyModelB_{nullptr};
    ui::InputText*                           nameIn_{nullptr};
    ui::Button*                              saveModelB_{nullptr};
    ui::Button*                              resetB_{nullptr};
    ui::Button*                              useB_{nullptr};
    ui::Button*                              closeB_{nullptr};
    core::ConnectionScope                    links_;
};

} // namespace app::ai
