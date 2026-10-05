// =============================================================================
//  ui/widgets/FileExplorer.hpp - lot API 8 : l'explorateur de fichiers de l'appli
// -----------------------------------------------------------------------------
//  LE DIALOGUE QUI REMPLACE CELUI DU SYSTEME derriere ui::pickFile (le bouton
//  ... des champs de chemin, Ouvrir un projet, les exports qui demandent ou) -
//  au theme de l'appli, qui connait le projet (CE PROJET, RECENTS, EPINGLES,
//  CE PC) et dit ce que sont les fichiers d'un automaticien (ui/FileKinds).
//  La conception : DESIGN-EXPLORATEUR.md (lot API 8).
//
//    - le titre de la demande, la croix ;
//    - la barre : Precedent, Suivant, Dossier parent, le fil d'Ariane cliquable
//      (un clic dans son vide, ou Ctrl+L : le champ du chemin, ou l'on tape ou
//      colle un chemin - Entree ; les dossiers se completent), Chercher (filtre
//      le dossier en direct), Details / Vignettes, Epingler, Nouveau dossier ;
//    - a gauche les Emplacements ; au centre la liste (TableView : Nom,
//      Modifie, Type, Taille ; les dossiers d'abord ; un clic sur un titre
//      trie ; les fichiers que le filtre cache sont comptes sur une ligne
//      "N autres fichiers caches par le filtre - Tout afficher") ;
//    - en bas le nom du fichier, les types, Annuler et Ouvrir / Enregistrer /
//      Remplacer / Choisir "dossier" ; l'avertissement "existe deja", l'erreur
//      d'un nom impossible (le bouton grise) ; la ligne d'etat.
//
//  LA MEME API QUE CELLE DU SYSTEME : `done(chemin)` quand on choisit,
//  `done("")` quand on annule (ou que le dialogue disparait sans reponse).
//  Le chemin rendu est brut : ui::finishPick (PathBrowse) fait le reste,
//  comme avant (l'extension d'un enregistrement l'est deja ici).
//
//  CLAVIER : fleches, Entree (ouvre le dossier ou choisit le fichier), Retour
//  arriere (parent), les premieres lettres choisissent, F5 relit, Ctrl+L le
//  chemin, Ctrl+F chercher, Echap annule. Double-clic : ouvre ou choisit.
//
//  SCRIPTS (ScriptRunner, commandes explorateur-*) : navigate, select,
//  setFilter, setThumbnails, setFileName, accept, cancel ; lines() dit ce que
//  montre la liste.
// =============================================================================
#pragma once

#include "../../menu/IMenu.hpp"
#include "../FileExplorerModel.hpp"
#include "../Widget.hpp"
#include "Controls.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui {

    class FileExplorerPreview;   // Lot API 8 : l'explorateur, 2e partie
    class FileExplorerBody;
    class FileExplorerTable;
    class FileExplorerPlaces;
    class FileExplorerGrid;
    class FileExplorerTableModel;

    // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu, les vignettes) ----
    //  Une image decodee (RGBA, 4 octets par pixel, ramenee a quelques centaines
    //  de pixels) : la vignette d'une image, dans l'apercu et la vue Vignettes.
    struct PreviewImage {
        int                       width{0}, height{0};          // les pixels ci-dessous
        std::uint32_t             fileWidth{0}, fileHeight{0};  // l'image du fichier
        std::vector<std::uint8_t> rgba;
    };
    //  Ce que montre la colonne Apercu pour l'element choisi (app/FilePreview).
    struct FilePreviewInfo {
        std::string                                      path;
        std::string                                      name;       // le nom de l'element
        std::string                                      kind;       // "Export Control Expert", "Dossier"
        bool                                             folder{false};
        std::vector<std::pair<std::string, std::string>> facts;      // ("Automate", "BMX P34 2020")
        std::vector<std::string>                         notes;      // "d\xC3\xA9j\xC3\xA0 import\xC3\xA9 dans ce projet le ..."
        std::vector<std::string>                         sheets;     // les onglets d'un classeur
        std::vector<std::vector<std::string>>            cells;      // le debut du premier onglet (4 x 4)
        std::vector<std::string>                         text;       // les 8 premieres lignes (chasse fixe)
        std::shared_ptr<const PreviewImage>              image;      // la vignette d'une image
        bool                                             openable{false};  // "Ouvrir avec le programme du syst\xC3\xA8me"
        std::string                                      error;      // illisible : pourquoi
    };
    // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    // Ce que l'appli sait et que l'explorateur ne peut pas deviner.
    struct FileExplorerContext {
        std::string projectDir;                        // CE PROJET ("" : pas de projet ouvert)
        std::string sourceXpg;                         // le .XPG d'origine du projet
        std::shared_ptr<files::Memory> memory;         // les recents, les epingles, les vues (peut manquer)
        std::function<void()> memoryChanged;           // l'appli l'ecrit dans ses reglages
        // ---- Lot API 8 : l'explorateur, 2e partie ----
        //  Tous facultatifs (sans eux : pas d'apercu, des icones, pas de lien).
        std::function<FilePreviewInfo(const std::string& path)> preview;          // l'apercu (en differe)
        std::function<std::shared_ptr<const PreviewImage>(const std::string& path, int maxSide)> thumbnail;
        std::function<std::string(const std::string& path)> openExternally;      // "" : ouvert ; sinon pourquoi
        std::function<void()> useSystemExplorer;                                  // le lien du bas
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----
    };

    class FileExplorerDialog final : public menu::WidgetMenu {
    public:
        FileExplorerDialog(FilePick pick, FileExplorerContext context, std::function<void(std::string)> done);
        ~FileExplorerDialog() override;

        [[nodiscard]] menu::MenuTraits traits() const override;
        [[nodiscard]] std::string title() const override;
        ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
        void Update(const menu::FrameContext& fc) override;

        // ---- la navigation ------------------------------------------------------
        //  "" : Ce PC (les emplacements et les lecteurs). Un chemin de fichier :
        //  son dossier, le fichier choisi. Faux (why) : le dossier ne se lit pas
        //  (il reste le precedent, l'erreur est dite dans la liste).
        bool navigate(const std::string& path, std::string* why = nullptr);
        bool back();
        bool forward();
        bool up();
        void refresh();                                   // F5
        void createFolder();                              // Nouveau dossier
        void togglePin();                                 // epingler / desepingler le dossier ouvert

        // ---- pour les scripts et les tests ---------------------------------------
        //  Un element du dossier par son nom (sans casse ; le debut suffit s'il
        //  est seul). Faux (why) : introuvable.
        bool select(const std::string& name, std::string* why = nullptr);
        //  Un type de la liste par le debut de son nom ("Classeurs", "Tous").
        bool setFilter(const std::string& prefix, std::string* why = nullptr);
        void setThumbnails(bool on);
        void setSearch(const std::string& text);
        void setFileName(const std::string& name);
        void showAllFiltered(bool on);                    // "Tout afficher"
        //  Le bouton principal. Faux (why) : rien a choisir, un nom impossible,
        //  un fichier introuvable.
        bool accept(std::string* why = nullptr);
        void cancel();

        [[nodiscard]] files::PickMode mode() const noexcept { return mode_; }
        [[nodiscard]] const std::string& folder() const noexcept { return folder_; }
        [[nodiscard]] bool thumbnails() const noexcept { return thumbnails_; }
        [[nodiscard]] std::string primaryLabel() const;
        [[nodiscard]] std::string statusText() const;
        [[nodiscard]] std::string message() const;        // l'avertissement ou l'erreur sous le nom
        //  Ce que montre la liste, une ligne par element : "[D] exports",
        //  "[F] MAST.XPG | Export Control Expert", "(7 autres fichiers caches...)".
        [[nodiscard]] std::vector<std::string> lines() const;
        [[nodiscard]] const std::vector<files::Place>& places() const noexcept { return places_; }

        // ---- Lot API 8 : l'explorateur, 2e partie (l'apercu) ----
        //  La colonne Apercu (260 px) : montree ou repliee (retenu par genre).
        void setPreviewShown(bool on);
        [[nodiscard]] bool previewShown() const noexcept { return previewShown_; }
        //  L'apercu de l'element choisi ; s'il attend son delai, calcule tout de suite.
        [[nodiscard]] const FilePreviewInfo& currentPreview();
        //  Ce que montre l'apercu, une ligne par information (scripts, tests).
        [[nodiscard]] std::vector<std::string> previewLines();
        //  "Ouvrir avec le programme du systeme" sur l'element choisi ; "" : ouvert.
        std::string openSelectedExternally();
        //  Les vignettes deja decodees (vue Vignettes) : combien, pour les tests.
        [[nodiscard]] std::size_t thumbnailCount() const noexcept { return thumbs_.size(); }
        //  Le menu du clic droit sur l'element choisi : Epingler / Desepingler (un
        //  dossier), Ouvrir avec le programme du systeme (un fichier), Copier le
        //  chemin. rowMenuChoose : une entree par le debut de son libelle (scripts).
        [[nodiscard]] std::vector<std::string> rowMenuLabels() const;
        bool rowMenuChoose(const std::string& labelPrefix, std::string* why = nullptr);
        void openRowMenu(gfx::Point at);
        //  Le lien du bas "Utiliser l'explorateur du systeme" : le reglage change
        //  (les prochaines demandes) ; faux (why) : l'appli ne le permet pas ici.
        bool chooseSystemExplorer(std::string* why = nullptr);
        [[nodiscard]] bool systemExplorerChosen() const noexcept { return systemChosen_; }
        //  La case "et ses sous-dossiers" de Chercher : la recherche descend dans les
        //  sous-dossiers (5 000 elements, 300 ms au plus) ; les noms montres sont
        //  relatifs au dossier ouvert ("sous/a.csv").
        void setSearchSubfolders(bool on);
        [[nodiscard]] bool searchSubfolders() const noexcept { return searchDeep_; }
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----

    protected:
        core::Status buildUi() override;

    private:
        friend class FileExplorerBody;
        friend class FileExplorerTable;
        friend class FileExplorerPlaces;
        friend class FileExplorerGrid;
        friend class FileExplorerTableModel;

        void load(bool keepSelection);                    // relire le dossier, refaire la liste
        void rebuildRows();                               // filtre, recherche, tri -> la liste
        void selectionChanged();
        void activateRow(std::size_t row);
        void sync();                                      // le bouton, le message, le nom
        void finish(std::string path);
        void rememberView();
        void showPathField(bool on);
        void sortByColumn(std::size_t col);
        [[nodiscard]] const files::Entry* selectedEntry() const;
        [[nodiscard]] const files::FilterGroup* currentFilter() const;
        [[nodiscard]] std::string startFolder(std::string* selectName);

        FilePick                               pick_;
        FileExplorerContext                    context_;
        std::function<void(std::string)>       done_;
        bool                                   answered_{false};
        files::PickMode                        mode_{files::PickMode::Open};
        std::string                            kind_;
        std::vector<files::FilterGroup>        filters_;
        int                                    filterIndex_{0};

        std::string                            folder_;
        files::Listing                         listing_;
        std::vector<files::Entry>              rows_;       // ce que montre la liste
        std::size_t                            hiddenByFilter_{0};
        bool                                   showFiltered_{false};
        std::string                            search_;
        files::SortKey                         sort_{files::SortKey::Name};
        bool                                   ascending_{true};
        bool                                   thumbnails_{false};
        std::vector<std::string>               backStack_, forwardStack_;
        std::string                            notice_;     // "La cle E: n'est plus la" (on est remonte)
        std::string                            lastError_;  // le dernier refus du bouton principal
        std::string                            saveMessage_; // "existe deja : il sera remplace", ou le nom impossible
        bool                                   saveExists_{false};
        bool                                   messageIsError_{false};
        std::string                            space_;      // "D: 214 Go libres"
        std::vector<files::Place>              places_;
        std::int64_t                           now_{0};
        const Theme*                           theme_{nullptr};

        FileExplorerBody*                      body_{nullptr};
        FileExplorerTable*                     table_{nullptr};
        FileExplorerPlaces*                    placesView_{nullptr};
        FileExplorerGrid*                      grid_{nullptr};         // la vue Vignettes
        std::shared_ptr<FileExplorerTableModel> model_;
        InputText*                             pathField_{nullptr};
        InputText*                             searchField_{nullptr};
        InputText*                             nameField_{nullptr};
        DropDown*                              typeList_{nullptr};
        Button*                                backButton_{nullptr};
        Button*                                forwardButton_{nullptr};
        Button*                                upButton_{nullptr};
        Button*                                detailsButton_{nullptr};
        Button*                                thumbsButton_{nullptr};
        Button*                                pinButton_{nullptr};
        Button*                                newFolderButton_{nullptr};
        Button*                                cancelButton_{nullptr};
        Button*                                okButton_{nullptr};
        bool                                   pathMode_{false};
        bool                                   nameFromSelection_{false};
        std::string                            typed_;      // les premieres lettres
        double                                 typedAt_{0.0};
        double                                 clock_{0.0};
        core::ConnectionScope                  links_;

        // ---- Lot API 8 : l'explorateur, 2e partie ----
        friend class FileExplorerPreview;
        void schedulePreview();                           // l'element choisi a change : en differe
        void computePreview();
        void decodeThumbnails();                          // quelques vignettes par image (Update)
        FileExplorerPreview*                   previewView_{nullptr};
        bool                                   previewShown_{true};
        std::string                            previewWanted_;   // le chemin a montrer
        double                                 previewDue_{-1.0}; // < 0 : rien n'attend
        FilePreviewInfo                        preview_;
        std::map<std::string, std::shared_ptr<const PreviewImage>> thumbs_;   // nullptr : illisible
        std::vector<std::string>               thumbWanted_;     // les images visibles sans vignette
        void rowMenuAction(int id);
        PopupMenu*                             rowMenu_{nullptr};
        bool                                   systemChosen_{false};
        [[nodiscard]] std::vector<files::Entry> deepEntries() const;   // le dossier et ses sous-dossiers
        Checkbox*                              deepBox_{nullptr};
        bool                                   searchDeep_{false};
        // ---- fin Lot API 8 : l'explorateur, 2e partie ----
    };

} // namespace ui
