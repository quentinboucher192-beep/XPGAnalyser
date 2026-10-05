// =============================================================================
//  app/hmi/HmiDisplayPanes.hpp - ce que voit l'operateur (lot 13)
// -----------------------------------------------------------------------------
//  CONFIGURATION > LANGUES (HmiLanguagesPane) et CONFIGURATION > UNITES ET
//  FORMATS (HmiUnitsPane).
//
//  LES LANGUES. A GAUCHE, les langues du projet : la premiere est celle dans laquelle il est
//  ecrit ; chacune des autres dit combien de textes sont traduits. La langue de
//  demarrage est marquee. AU MILIEU, tous les textes que l'operateur lit (ceux
//  des objets, des listes, des etats, des titres de popup, les messages et les
//  consignes des alarmes), une colonne par langue : une case vide reste dans
//  la langue du projet (orange) ; une traduction qui perd un trou {...} est en
//  rouge. A DROITE, la fiche du texte choisi : une traduction par langue.
//
//  EXCEL : Exporter ecrit le tableau (Texte (fr), English (en)..., Ou) dans
//  exports/ pour un traducteur ; Importer le relit - une langue nouvelle
//  s'ajoute, une case videe retire la traduction. ESSAYER : la simulation
//  s'ouvre dans la langue choisie.
//
//  Chaque changement est une commande : Ctrl+Z.
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "../TablePaste.hpp"
#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiExport.hpp"
#include "../../hmi/HmiLanguages.hpp"
#include "../../hmi/HmiRuntime.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiLanguagesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiLanguagesPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // Les gestes du volet, pour les boutons, les scripts et les tests.
    bool addLanguage(const std::string& code, const std::string& name = {}, std::string* why = nullptr);
    bool removeLanguage(const std::string& code, std::string* why = nullptr);
    bool setLanguageName(const std::string& code, const std::string& name, std::string* why = nullptr);
    bool setStartLanguage(const std::string& code, std::string* why = nullptr);      // "" : la premiere
    // Une traduction ; vide : retiree (le texte reste dans la langue du projet).
    bool setTranslation(const std::string& source, const std::string& code, const std::string& text, std::string* why = nullptr);
    // Excel : le tableau dans exports/ ; le relire (un fichier, ou un tableau deja lu).
    bool exportTranslations(std::string* where = nullptr);
    bool importFile(const std::string& path, std::string* why = nullptr);
    bool importTable(const std::vector<std::string>& headers, const std::vector<std::vector<std::string>>& rows,
                     std::string* why = nullptr);
    [[nodiscard]] const hmi::TranslationImport& lastImport() const noexcept { return lastImport_; }

    // Seulement les textes qu'il reste a traduire (dans au moins une langue).
    void setOnlyMissing(bool on);
    [[nodiscard]] bool onlyMissing() const noexcept { return onlyMissing_; }

    [[nodiscard]] std::string selectedLanguage() const;     // son code ; "" : aucune
    [[nodiscard]] std::string selectedText() const;         // le texte d'origine ; "" : aucun
    void selectLanguage(const std::string& code);
    void selectText(const std::string& source);

    struct Hosts {
        std::function<bool(const hmi::ExportRequest&, std::string*)> exportFile;   // dans exports/
        std::function<void()>                                         askImport;    // demander le fichier
        std::function<void()>                                         askAdd;       // demander la langue
        std::function<void(const std::string& code)>                  tryIt;        // la simulation dans cette langue
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     languages() noexcept { return *langs_; }
    [[nodiscard]] ui::TableView&     texts() noexcept { return *texts_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refreshTexts();
    void rebuildProperties();
    void say(std::string text, bool error = false);
    bool change(const std::string& label, const std::function<void(hmi::Languages&)>& fn);
    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    Hosts                 hosts_;
    HmiToolStrip*         tools_{nullptr};
    ui::TableView*        langs_{nullptr};
    ui::TableView*        texts_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    std::shared_ptr<ui::ITableModel> langsModel_, textsModel_;
    std::vector<std::string>         langOrder_;        // les codes, dans l'ordre du tableau
    std::vector<hmi::TranslatableText> shownTexts_;     // les textes montres
    bool                  onlyMissing_{false};
    bool                  refreshing_{false};
    std::string           message_;
    hmi::TranslationImport lastImport_;
    paste::Binding        paste_;          // lot 20 : coller des traductions depuis Excel
    core::ConnectionScope links_;
};

// =============================================================================
//  LES UNITES ET LES FORMATS. Une ligne par variable (IHM ou de l'automate ;
//  "Armoires[].ana.PT1.mes" vaut pour chaque case) : son unite, son format, un
//  exemple, et ou elle se montre. Elles sont reprises partout ou la variable
//  s'affiche (HmiDisplay.hpp). PROPOSER relit les vues : chaque variable montree
//  avec une unite ou un format par un objet devient une ligne.
// =============================================================================
class HmiUnitsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiUnitsPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    bool addDisplay(const std::string& path, const std::string& unit = {}, const std::string& format = {}, std::string* why = nullptr);
    bool removeDisplay(const std::string& path, std::string* why = nullptr);
    // "variable", "unite", "format".
    bool setField(const std::string& path, const std::string& key, const std::string& value, std::string* why = nullptr);
    // Les variables montrees avec une unite ou un format par les objets : une
    // ligne chacune (celles qui en ont deja une sont laissees). Rend le nombre ajoute.
    std::size_t proposeFromViews();

    [[nodiscard]] std::string selectedDisplay() const;       // son chemin ; "" : aucune
    void selectDisplay(const std::string& path);

    struct Hosts {
        std::function<void()> askAdd;                          // demander la variable, l'unite, le format
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }

    [[nodiscard]] HmiToolStrip&      tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&     table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid&  properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool error = false);
    hmi::DocumentPtr      doc_;
    Apply                 apply_;
    Hosts                 hosts_;
    HmiToolStrip*         tools_{nullptr};
    ui::TableView*        table_{nullptr};
    ui::PropertyGrid*     grid_{nullptr};
    ui::StatusBar*        status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<std::string> order_;
    bool                  refreshing_{false};
    std::string           message_;
    paste::Binding        paste_;          // lot 20 : coller depuis Excel
    core::ConnectionScope links_;
};

} // namespace app
