// =============================================================================
//  app/hmi/HmiReportLayoutPane.hpp - 1.25.0 : Configuration > Rapports > Mise en page
// -----------------------------------------------------------------------------
//  LE CLIENT (05/11, maquette validee) : << configurer la mise en page du fichier
//  genere (onglet Mise en page), avec apercu et sauvegarde des mises en page, et
//  quand on exporte on puisse choisir la mise en page >>.
//
//    | MISES EN PAGE     | REGLAGES                   | APERCU  [Exemple|Derniere] < Page 1/3 >  - 75 % + |
//    | + Nouvelle  Dupl. | Page : format, orientation | +---------------------------+                    |
//    | FOURNIES          |   marges                   | | (logo) Rapport journalier |                    |
//    |  (c) Standard *   | En-tete : logo, titre...   | | Synthese ...              |                    |
//    |  (c) Compacte     | Pied de page, visas        | +---------------------------+                    |
//    | DU PROJET         | Tableaux, Texte            |                                                   |
//    |  (p) Exploitation | CONTENU (coche, glisse)    |                                                   |
//    | Utilisee par : .. |                            |                                                   |
//    +-------------------+----------------------------+---------------------------------------------------+
//    | Exploitation : modifiee (non enregistree) - utilisee par 1 rapport  [Annuler les modifications] [Enregistrer] |
//
//  LES FOURNIES (Standard, Compacte, Synthese seule) sont verrouillees : leurs
//  reglages se lisent, << Dupliquer a partir de... >> en fait une du projet. UNE
//  MISE EN PAGE DU PROJET SE REGLE EN BROUILLON : chaque reglage change l'apercu
//  tout de suite, Ctrl+Z le defait (dans le volet) ; << Enregistrer la mise en
//  page >> le garde dans le projet (une commande : Ctrl+Z ensuite la rend comme
//  avant), << Annuler les modifications >> revient a l'enregistree. Les rapports
//  qui l'emploient la prennent a leur prochaine ecriture. Le point (un rond) dit
//  qu'une mise en page a un brouillon ; l'etoile, celle par defaut (celle d'un
//  nouveau rapport). Supprimer : plusieurs d'un coup, un seul Ctrl+Z ; les
//  rapports qui l'employaient prennent celle par defaut.
//
//  L'APERCU dessine la page calculee (hmi/HmiReportLayout.hpp, paginate) - celle
//  que le PDF ecrit - avec des donnees d'exemple, ou la derniere periode reelle
//  (l'historique du projet) d'un rapport qui l'emploie.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiReportLayout.hpp"
#include "../../ui/Widget.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui { class StatusBar; }

namespace app {

class HmiToolStrip;

// ---- l'apercu : une page calculee, dessinee a l'echelle ------------------------------------------------------------
class HmiReportPreview final : public ui::Widget {
public:
    explicit HmiReportPreview(std::string id);
    void setDocument(std::shared_ptr<const hmi::report::PageDoc> doc);
    [[nodiscard]] const hmi::report::PageDoc* document() const noexcept { return doc_.get(); }
    void setPage(std::size_t page);
    [[nodiscard]] std::size_t page() const noexcept { return page_; }
    [[nodiscard]] std::size_t pageCount() const noexcept { return doc_ ? doc_->pages.size() : 0; }
    // Le zoom en pour cent de la taille reelle (96 points par pouce) ; 0 : la page a la largeur (une vignette : entiere).
    void setZoomPercent(int percent);
    [[nodiscard]] int zoomPercent() const noexcept { return zoom_; }
    [[nodiscard]] int shownZoom() const;                 // le pourcentage montre (a la largeur : calcule)
    void setThumbnail(bool on) { thumbnail_ = on; invalidate(); }
    [[nodiscard]] gfx::Rect pageRect() const noexcept { return pageRect_; }   // la page a l'ecran (essais)
    // Les textes dessines (UTF-8), pour les essais : ceux de la page montree, dans l'ordre.
    [[nodiscard]] std::vector<std::string> shownTexts() const;

protected:
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    [[nodiscard]] double scaleFor(const gfx::Rect& area) const;
    std::shared_ptr<const hmi::report::PageDoc> doc_;
    std::size_t                 page_{0};
    int                         zoom_{0};
    float                       scrollY_{0};
    bool                        thumbnail_{false};
    gfx::Rect                   pageRect_{};
    std::vector<gfx::TextureId> textures_;              // le logo, par renderer
    const hmi::report::PageDoc* texturesFor_{nullptr};
};

// ---- l'onglet Mise en page ------------------------------------------------------------------------------------------
class HmiReportLayoutPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Action : int {
        ActNew = 1, ActDuplicate, ActRemove, ActDefault,          // la liste
        ActSample, ActLast, ActPrev, ActPage, ActNext, ActZoomOut, ActZoomShown, ActZoomIn,   // l'apercu
        ActCancel, ActSave,                                       // le bas
        ActUp, ActDown                                            // les parties
    };
    HmiReportLayoutPane(std::string id, hmi::DocumentPtr doc, Apply apply);
    void refresh();

    // ---- les gestes (les boutons, les scripts, les essais) ----
    bool selectLayout(const std::string& name);
    [[nodiscard]] std::string selectedLayout() const;
    [[nodiscard]] std::vector<std::string> listedLayouts() const;          // fournies, puis du projet
    bool newLayout(std::string* why = nullptr);                            // + Nouvelle : une copie de Standard
    bool duplicateLayout(const std::string& from, std::string* why = nullptr);
    bool removeLayout(const std::string& name, std::string* why = nullptr);
    bool removeChosen();                                                   // celles choisies (plusieurs d'un coup)
    bool setDefaultLayout(const std::string& name, std::string* why = nullptr);
    // Un reglage du brouillon de la mise en page choisie : "nom", "format", "orientation", "marge_haut", "marge_bas",
    // "marge_gauche", "marge_droite", "logo", "logo_place", "titre", "sous_titre", "accent", "pied_gauche",
    // "pied_centre", "pied_droite", "visa", "visas", "taille_tableaux", "lignes_alternees", "entete_repete", "police",
    // "taille_titre", "taille_corps".
    bool setField(const std::string& key, const std::string& value, std::string* why = nullptr);
    bool setPart(const std::string& key, bool on, std::string* why = nullptr);
    bool movePart(const std::string& key, std::size_t rank, std::string* why = nullptr);
    bool undoDraft();
    bool redoDraft();
    bool save(std::string* why = nullptr);                                 // Enregistrer la mise en page
    void cancel();                                                         // Annuler les modifications
    [[nodiscard]] bool modified() const;                                   // la choisie a un brouillon different
    [[nodiscard]] bool modified(const std::string& name) const;
    [[nodiscard]] bool locked() const;                                     // une fournie
    [[nodiscard]] hmi::ReportLayout current() const;                       // le brouillon, sinon l'enregistree
    void setPreviewSample(bool sample);
    [[nodiscard]] bool previewSample() const noexcept { return sample_; }
    void setPreviewReport(std::string report);
    [[nodiscard]] std::vector<ui::PopupMenu::Item> duplicateItems() const;   // << Dupliquer a partir de... >>
    [[nodiscard]] std::string statusText() const;                          // la ligne du bas

    [[nodiscard]] HmiToolStrip&       tools() noexcept { return *tools_; }
    [[nodiscard]] HmiToolStrip&       previewTools() noexcept { return *previewTools_; }
    [[nodiscard]] HmiToolStrip&       bottomTools() noexcept { return *bottom_; }
    [[nodiscard]] ui::TableView&      list() noexcept { return *list_; }
    [[nodiscard]] ui::PropertyGrid&   settings() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&      parts() noexcept { return *parts_; }
    [[nodiscard]] HmiReportPreview&   preview() noexcept { return *preview_; }
    [[nodiscard]] const std::string&  message() const noexcept { return message_; }

    // La liste des mises en page a change (une de plus, une de moins, renommee, celle par defaut) : le volet des
    // rapports refait sa colonne et ses choix.
    const core::SignalPtr<> layoutsChanged = core::Signal<>::create();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;   // Ctrl+Z, Ctrl+Y : le brouillon

private:
    struct Draft {
        hmi::ReportLayout              layout;
        std::vector<hmi::ReportLayout> undo, redo;
    };
    struct Line {
        bool        header{false};
        std::string text;
        std::string name;              // une mise en page
        bool        builtIn{false};
    };
    void rebuildList();
    void rebuildSettings();
    void rebuildParts();
    void updatePreview();
    void say(std::string text, bool error = false);
    bool fail(std::string text, std::string* why);
    bool change(const std::string& label, const std::function<void(hmi::Project&)>& fn);
    bool editDraft(const std::function<void(hmi::ReportLayout&)>& fn, std::string* why);
    [[nodiscard]] const hmi::ReportLayout* stored(const std::string& name) const;   // du projet, sinon fournie
    [[nodiscard]] std::vector<std::string> chosenNames() const;

    hmi::DocumentPtr                  doc_;
    Apply                             apply_;
    HmiToolStrip*                     tools_{nullptr};
    HmiToolStrip*                     previewTools_{nullptr};
    HmiToolStrip*                     bottom_{nullptr};
    ui::TableView*                    list_{nullptr};
    ui::PropertyGrid*                 grid_{nullptr};
    ui::TableView*                    parts_{nullptr};
    HmiReportPreview*                 preview_{nullptr};
    ui::PopupMenu*                    menu_{nullptr};
    std::shared_ptr<ui::ITableModel>  listModel_, partsModel_;
    std::vector<Line>                 lines_;
    std::vector<std::string>          partKeys_;          // l'ordre montre des parties
    std::map<hmi::Id, Draft>          drafts_;
    std::string                       selected_{"Standard"};
    std::string                       previewReport_;
    bool                              sample_{true};
    bool                              refreshing_{false};
    std::string                       message_;
    gfx::Rect                         listFoot_{}, bottomText_{}, partsHead_{};
    core::ConnectionScope             links_;
};

} // namespace app
