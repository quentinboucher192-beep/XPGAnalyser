// =============================================================================
//  app/MacrosPane.hpp - l'onglet Macros (lot macros 1)
// -----------------------------------------------------------------------------
//  LANCER UNE MACRO SANS LIRE SON CODE. Un onglet au centre, comme Versions :
//
//    A GAUCHE, LA LISTE : les macros rangees en dossiers (comme les fonctions
//    IHM dans l'IHM), une recherche, Toutes / Favorites / Recentes. On y
//    cree, renomme, duplique, supprime (dans la corbeille : Restaurer la
//    remet) et l'on y glisse une macro ou un dossier dans un autre.
//
//    A DROITE, LA FICHE de la macro choisie : ce qu'elle fait, ce qu'elle lit,
//    ce qu'elle produit, les macros qu'elle enchaine, ce qu'elle va demander,
//    ses profils de reponses. Lancer (Entree, ou double-clic dans la liste),
//    Modifier le code, Aide.
//
//    LANCER ouvre le formulaire a la place de la fiche : 1 Les questions,
//    2 L'apercu, 3 Applique. Chaque reponse relance l'apercu (la macro tourne
//    pour de vrai et defait tout) ; les compteurs du bas suivent. Appliquer
//    pose UNE commande : un Ctrl+Z reprend tout.
//
//  Le volet ne pose aucun dialogue lui-meme : il les demande a l'ecran (Hosts).
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../core/Signal.hpp"
#include "../domain/ProjectModel.hpp"
#include "../project/MacroFolders.hpp"
#include "../project/MacroMemory.hpp"
#include "../project/MacroSession.hpp"
#include "../project/MacroSpec.hpp"
#include "../project/SharedLibrary.hpp"
#include "../ui/Widget.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ui {
class TreeView;
class InputText;
class ToggleButton;
class TableView;
class Button;
class StatusBar;
}

namespace app {

class HmiToolStrip;
class MacroFormView;
class MacroTreeModel;
class MacroCard;
class MacroRunView;
class MacroLegend;
class MacroEditorView;
namespace macroui { class Segmented; }

class MacrosPane final : public ui::Widget {
public:
    // Les gestes de la barre (et du menu contextuel de la liste).
    enum Action : int { ANewMacro = 1, ANewFolder, ADuplicate, ARename, ADelete, ARestore, AFavourite, AEdit, ALaunch, AHelp, APurge };

    struct Hosts {
        std::function<std::shared_ptr<domain::Project>()> project;     // le document ; nul : pas de projet
        std::function<std::string()>                     projectKey;  // les reponses retenues vont par projet
        // Pose la commande sur la pile (un Ctrl+Z), puis rafraichit l'ecran.
        std::function<void(core::CommandPtr, const std::string& summary)> apply;
        std::function<void(const std::string& macro)> editCode;
        std::function<void(const std::string& macro)> help;
        // Les dialogues : l'ecran les pose, puis appelle reload().
        std::function<void(const std::string& folder)>           newMacro;
        std::function<void(const std::string& parent)>           newFolder;
        std::function<void(const std::string& macro)>            renameMacro;
        std::function<void(const std::string& macro)>            duplicateMacro;
        std::function<void(const std::string& macro)>            deleteMacro;
        std::function<void(const std::string& folder)>           renameFolder;
        std::function<void(const std::string& folder)>           deleteFolder;
        std::function<void(const std::string& macro)>            restoreMacro;
        std::function<void(const std::string& macro)>            purgeMacro;
        std::function<void(const std::string& macro, const std::map<std::string, std::string>& answers)> saveProfile;
        // La bibliotheque a change (un rangement, une macro creee) : l'arbre suit.
        std::function<void()> libraryChanged;
        // Lot API 6 : le mode Modifier, dans cet onglet - l'ecran fabrique
        // l'editeur (il sait l'enregistrer, l'essayer, demander) ; nul : Modifier
        // ouvre l'onglet "Macro X" d'avant (editCode).
        std::function<std::unique_ptr<MacroEditorView>(const std::string& macro)> makeEditor;
        // La commande posee par Appliquer est-elle encore faite ? (Ctrl+Z l'a
        // peut-etre reprise.) L'adresse n'est jamais dereferencee.
        std::function<bool(const void* command)> stillApplied;
        // Non vide : Appliquer est refuse, et voici pourquoi (projet verrouille).
        std::function<std::string()> refuseApply;
        std::function<void(const std::string& text, bool error)> status;
    };

    MacrosPane(std::string id, std::string libsRoot, project::macro::MacroMemory* memory);
    ~MacrosPane() override;

    void setHosts(Hosts hosts) { hosts_ = std::move(hosts); }
    [[nodiscard]] const std::string& libsRoot() const noexcept { return libsRoot_; }

    // Relit libs/ : les macros, leurs lignes #!, les dossiers, la corbeille.
    void reload();
    // La fiche d'une macro (choisie dans la liste).
    void select(const std::string& macro);
    void selectFolder(const std::string& folder);
    // Le formulaire. `source` : le code a lancer (celui de l'editeur, pas encore
    // enregistre) ; vide : celui de libs/. `profile` : un jeu de reponses garde.
    void launch(const std::string& macro, std::string source = {}, const std::string& profile = {});
    void closeRun();
    // Lot API 6 : UTILISER / MODIFIER, dans le meme onglet. Modifier montre le
    // mode editeur de la macro (le code, les cartes, l'apercu, l'essai) a la
    // place de la liste et de la fiche ; Utiliser revient a la liste. Chaque
    // macro garde son editeur (et ce qui n'est pas enregistre) jusqu'a la
    // fermeture de l'onglet.
    void edit(const std::string& macro);
    void use();
    [[nodiscard]] bool editing() const noexcept { return !editing_.empty(); }
    [[nodiscard]] const std::string& editedMacro() const noexcept { return editing_; }
    [[nodiscard]] MacroEditorView* editor() const;
    [[nodiscard]] macroui::Segmented& modeSwitch() noexcept { return *mode_; }
    // Chaque image : l'apercu differe apres une frappe.
    void tick(double now);

    // ---- pour l'ecran, les scripts et les tests ----------------------------------
    [[nodiscard]] std::string selectedMacro() const;
    [[nodiscard]] std::string selectedFolder() const;
    [[nodiscard]] bool trashSelected() const;
    [[nodiscard]] int  step() const noexcept { return step_; }      // 0 fiche, 1 questions, 2 apercu, 3 applique
    [[nodiscard]] ui::TreeView& tree() noexcept { return *tree_; }
    [[nodiscard]] MacroFormView* form() const noexcept;
    [[nodiscard]] project::macro::MacroSession* session() const noexcept { return session_.get(); }
    [[nodiscard]] const project::macro::FolderLayout& layout() const noexcept { return layout_; }
    [[nodiscard]] const project::macro::MacroSpec* spec(const std::string& macro) const;
    [[nodiscard]] const std::vector<project::SharedLibrary::TrashedMacro>& trash() const noexcept { return trash_; }
    [[nodiscard]] HmiToolStrip& tools() noexcept { return *tools_; }
    // Une ligne de la liste par son chemin ("Importer depuis le classeur/ImporterClasseur").
    [[nodiscard]] ui::NodeId nodeOf(const std::string& path) const;
    void runAction(int action);
    // Les etapes, comme les boutons.
    void goPreview();
    void goBack();
    bool applyNow();
    // Une reponse, comme la souris (le formulaire refait l'apercu tout de suite).
    bool answer(const std::string& key, const std::string& value);
    void refreshNow();
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    // Un apercu attend d'etre refait (une frappe recente) : les scripts attendent.
    [[nodiscard]] bool busy() const noexcept { return refreshPending_; }

    // ---- lot API 2 : les pastilles et la legende ----------------------------------
    //  Sept pastilles par macro, ce qu'elle touche (MacroSpec::Touches) : lit le
    //  classeur, lit un CSV, enchaine d'autres macros, importe de la bibliotheque,
    //  ecrit des sections, cree des variables, ecrit un fichier a cote. Un clic
    //  sur une pastille de la legende ne garde que les macros qui l'ont.
    static constexpr int kPipCount = 7;
    [[nodiscard]] static std::string_view pipLabel(int pip) noexcept;     // "lit le classeur"
    [[nodiscard]] static bool pipOn(const project::macro::MacroSpec&, int pip) noexcept;
    void setPipFilter(int pip);                    // -1 : aucune
    [[nodiscard]] int pipFilter() const noexcept { return pipFilter_; }
    void setLegendOpen(bool open);
    [[nodiscard]] bool legendOpen() const noexcept { return legendOpen_; }
    [[nodiscard]] gfx::Rect legendPipRect(int pip) const;

protected:
    void            onLayout() override;
    void            onPaint(const ui::PaintContext&) override;
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    friend class MacroCard;
    friend class MacroRunView;
    friend class MacroTreeModel;
    friend class MacroLegend;
    void selectNode(ui::NodeId);
    void rebuildTree();
    void applyFilter();
    void onTreeSelection(ui::NodeId);
    void onTreeActivated(ui::NodeId);
    void showContextMenu(ui::NodeId, gfx::Point);
    void refreshCard();
    void scheduleRefresh(bool immediate);
    void doRefresh(bool exact);
    void onFieldChanged(const std::string& key, const std::string& value, bool immediate);
    void say(std::string text, bool error = false);
    void wireDrag();
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> namesOf(project::macro::FieldKind) const;
    [[nodiscard]] std::string workbookPath() const;
    [[nodiscard]] std::vector<std::string> readsOf(const std::string& macro, bool optional) const;

    std::string                                     libsRoot_;
    project::macro::MacroMemory*                    memory_{nullptr};
    Hosts                                           hosts_;
    std::unique_ptr<project::SharedLibrary>         library_;
    std::map<std::string, project::macro::MacroSpec> specs_;
    project::macro::MacroFolders                    folders_;
    project::macro::FolderLayout                    layout_;
    std::vector<project::SharedLibrary::TrashedMacro> trash_;

    HmiToolStrip*                    tools_{nullptr};
    MacroLegend*                     legend_{nullptr};     // lot API 2
    int                              pipFilter_{-1};
    bool                             legendOpen_{true};
    ui::InputText*                   search_{nullptr};
    std::vector<ui::ToggleButton*>   chips_;
    int                              chip_{0};          // 0 toutes, 1 favorites, 2 recentes
    bool                             syncingChips_{false};
    ui::TreeView*                    tree_{nullptr};
    std::shared_ptr<MacroTreeModel>  treeModel_;
    MacroCard*                       card_{nullptr};
    MacroRunView*                    run_{nullptr};
    ui::Widget*                      contextMenu_{nullptr};
    ui::NodeId                       contextNode_{0};

    std::string                      current_;          // la macro de la fiche / du formulaire
    std::string                      currentFolder_;
    std::unique_ptr<project::macro::MacroSession> session_;
    int                              step_{0};
    bool                             refreshPending_{false};
    double                           refreshAt_{0.0};
    double                           now_{0.0};
    std::string                      message_;
    project::MacroReport             applied_;          // le compte rendu de l'Appliquer
    const void*                      appliedCommand_{nullptr};   // son adresse, pour savoir si Ctrl+Z l'a reprise
    std::string                      appliedSummary_;
    bool                             builtOnce_{false};
    std::map<std::string, MacroEditorView*> editors_;   // lot API 6 : par macro
    std::string                      editing_;          // la macro en mode Modifier ; vide : Utiliser
    macroui::Segmented*              mode_{nullptr};
    bool                             reorderPending_{false};
    void                             applyMode();
    core::ConnectionScope            links_;
};

} // namespace app
