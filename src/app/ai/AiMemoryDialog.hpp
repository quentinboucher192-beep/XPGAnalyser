// =============================================================================
//  app/ai/AiMemoryDialog.hpp - LA FENETRE "MEMOIRE ET APPRENTISSAGE DE L'IA"
// -----------------------------------------------------------------------------
//  La commande ia.memoire l'ouvre (Ctrl+K "Memoire et apprentissage de l'IA" ;
//  les parametres de l'Assistant ont un bouton qui la declenche). Une fenetre du
//  logiciel comme Historique ou Disposition : elle se deplace par son titre,
//  s'agrandit par un bord, Echap la ferme.
//
//    +-- Memoire et apprentissage de l'IA ---------------- Donnees de l'Assistant sur ce PC --+
//    | Vue generale | Apprentissage | Memoire | Stockage PC | Nettoyage | Activite             |
//    |  ...                                                                                    |
//    +-----------------------------------------------------------------------------------------+
//    | Cache vide : 1,2 Mo liberes.                               [Actualiser]  [Fermer]       |
//    +-----------------------------------------------------------------------------------------+
//
//  TOUT EST REEL (le moteur : AiMemory.hpp, AiKnowledge.hpp) : chaque chiffre se
//  calcule sur les fichiers de IA/, le disque et le processus ; apres chaque
//  operation, tout se remesure (refresh) - les jauges suivent.
//
//    - VUE GENERALE : l'etat de la memoire, les comptes (conversations, messages,
//      connaissances, genres connus 116/116), les tailles, l'espace libre ; deux
//      blocs separes, la memoire vive de l'application et le stockage permanent ;
//      le modele distant n'occupe rien sur ce PC, c'est dit ;
//    - APPRENTISSAGE (1.17.0 : un tableau de bord) : ce qu'elle sait et qu'elle
//      progresse - le bandeau honnete, Importer... / Exporter l'apprentissage...
//      (un fichier .xia), quatre tuiles, deux graphiques mesures sur ce PC ;
//      en bas, les THEMES (Modeles metier, Vocabulaire, les categories des
//      connaissances, A lui apprendre), la liste du theme (Rechercher, filtres,
//      tri) et son detail MODIFIABLE (un modele : son nom, ses mots, ses
//      membres ; un mot : ce qu'il designe) ; << Essayer une phrase >>. Un
//      theme en texte garde la page des connaissances (consulter, modifier,
//      activer, desactiver, supprimer, ajouter) ;
//    - MEMOIRE : les informations persistantes par categorie, la recherche,
//      supprimer une conversation, une memoire, une categorie entiere ;
//    - STOCKAGE PC : la repartition (tableau et anneau), le total, l'espace libre,
//      l'emplacement (copiable), Ouvrir le dossier, les fichiers, le dernier nettoyage ;
//    - NETTOYAGE : Vider le cache (le detail avant), puis la zone rouge des donnees
//      persistantes ("Je comprends" coche, puis une confirmation) ;
//    - ACTIVITE : le journal, filtre par type.
//
//  Les actions destructrices sont en rouge et toujours confirmees (la bande de
//  confirmation au-dessus des boutons, comme dans la fenetre Historique).
// =============================================================================
#pragma once

#include "AiKnowledge.hpp"
#include "AiMemory.hpp"
#include "../../menu/IMenu.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace app {

class AiMemoryDialog final : public menu::WidgetMenu {
public:
    struct Spec {
        std::string id{"dialog.iaMemoire"};
        std::string project;      // le projet ouvert (son nom) ; vide : aucun
        // Ouvrir un dossier dans l'explorateur du systeme ; rend "" ou pourquoi pas. Vide :
        // app::dossiers::ouvrirDansLeSysteme.
        std::function<std::string(const std::string&)> openFolder;
        // 1.14.2 (captures) : ouverte par-dessus une autre fenetre modale qui assombrit deja le fond (les
        // parametres de l'Assistant) : pas de second voile, le fond n'est assombri qu'une fois (traits).
        bool overModal{false};
    };

    enum class Tab : std::uint8_t { Overview, Learning, Memory, Storage, Cleaning, Activity };
    // Les categories de l'onglet Memoire (12.3).
    enum class MemCat : std::uint8_t { Conversations, Messages, Projects, Preferences, Knowledge, Attachments, Search };

    explicit AiMemoryDialog(Spec spec);
    ~AiMemoryDialog() override;
    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string title() const override { return "M\xC3\xA9moire et apprentissage de l'IA"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override;
    void Update(const menu::FrameContext& fc) override;

    // ---- l'etat (ce que montre la fenetre ; essais, scripts) ------------------------------
    void refresh();                                     // tout remesurer (apres chaque operation)
    void refreshRam();                                  // la memoire vive seule (chaque seconde)
    [[nodiscard]] const ai::memory::Measure& measured() const noexcept { return measure_; }
    [[nodiscard]] const ai::memory::Ram& ram() const noexcept { return ram_; }
    [[nodiscard]] const ai::memory::Registry& registry() const noexcept { return registry_; }
    [[nodiscard]] const std::vector<ai::memory::Conversation>& conversations() const noexcept { return conversations_; }
    [[nodiscard]] const std::vector<ai::memory::Activity>& activity() const noexcept { return activity_; }
    [[nodiscard]] std::size_t messageCount() const noexcept;
    [[nodiscard]] const std::string& project() const noexcept { return spec_.project; }
    // Une ligne d'etat de la memoire (le haut de la Vue generale).
    [[nodiscard]] std::string stateText() const;

    // ---- les onglets ----------------------------------------------------------------------
    void showTab(Tab t);
    [[nodiscard]] Tab tab() const noexcept;
    // Par son titre ou son debut, sans casse ni accents ("Nettoyage", "stockage") ; faux : inconnu.
    bool showTab(std::string_view title);
    [[nodiscard]] static std::string tabTitle(Tab t);

    // ---- Apprentissage ----------------------------------------------------------------------
    void setCategoryFilter(std::string key);            // "" : toutes
    void setProjectFilter(std::string project);         // "" : tous ; "*" : communes a tous les projets
    [[nodiscard]] std::vector<const ai::memory::Knowledge*> shownKnowledge() const;
    bool selectKnowledge(std::string_view idOrName);    // l'id, le nom, ou le debut du nom
    [[nodiscard]] const ai::memory::Knowledge* selectedKnowledge() const;
    // Ajouter / modifier sans l'editeur (essais) ; l'editeur : beginAdd, beginEdit, saveEditor.
    bool addKnowledge(ai::memory::Knowledge k);
    bool updateSelected(const ai::memory::Knowledge& changes);
    bool toggleSelected();                              // activer / desactiver
    bool deleteSelected(bool now = false);              // une confirmation d'abord, sauf `now`
    void beginAdd();
    void beginEdit();
    void beginView();                                   // consulter (lecture seule)
    bool saveEditor();
    void closeEditor();
    [[nodiscard]] bool editorOpen() const noexcept;
    [[nodiscard]] bool editorReadOnly() const noexcept { return editorReadOnly_; }

    // ---- Apprentissage : le tableau de bord (1.17.0) -------------------------------------------
    // Un theme : "modeles", "vocabulaire", "apprendre" (les demandes non comprises) ou une categorie des
    // connaissances ("regles", "metier"...) ; faux : inconnu.
    bool showLearningTheme(std::string_view key);
    [[nodiscard]] std::string learningTheme() const;
    void setLearningSearch(std::string text);           // le theme et ses comptes ; les connaissances aussi
    [[nodiscard]] std::size_t learningRowCount() const; // la liste du theme (un theme en texte : ses connaissances)
    bool selectLearningRow(std::string_view key);       // un modele (sa cle ou son nom), un mot, une demande
    bool beginLearningNew();                            // Nouveau modele... / Nouveau mot...
    bool saveLearningDetail();                          // Enregistrer (ce que montre le detail)
    bool revertLearningDetail();                        // un modele du logiciel : revenir a l'original
    bool toggleLearningDetail();                        // activer / desactiver
    bool removeLearningDetail();
    // A lui apprendre : le mot choisi de la demande choisie = un genre ("genre", cle hmi::kindKey), un modele
    // ("modele", sa cle) ou un mot a ignorer ("ignorer").
    bool teachLearning(std::string_view word, std::string_view meaning, std::string_view target);
    bool ignoreLearningRequest();
    bool tryLearningPhrase(const std::string& phrase);  // << Essayer une phrase >> (rien ne s'applique)
    [[nodiscard]] std::string learningTrial() const;    // ce que l'essai a dit
    bool exportLearning(const std::string& path);       // un .xia (sans la cle API)
    bool importLearning(const std::string& path);       // ce que ferait l'import, puis la confirmation (confirmYes)

    // ---- Memoire ------------------------------------------------------------------------------
    void showMemoryCategory(MemCat c);
    [[nodiscard]] MemCat memoryCategory() const noexcept { return memCat_; }
    void setSearch(std::string text);
    [[nodiscard]] const std::vector<ai::memory::Hit>& hits() const noexcept { return hits_; }
    [[nodiscard]] std::size_t memoryRowCount() const;
    bool selectMemoryRow(std::string_view text);        // la premiere ligne qui contient ce texte
    bool deleteMemorySelection(bool now = false);       // une conversation, une memoire, un dossier joint
    bool deleteMemoryCategory(bool now = false);        // la categorie entiere
    [[nodiscard]] static std::string memCatTitle(MemCat c);

    // ---- Stockage PC --------------------------------------------------------------------------
    [[nodiscard]] std::string location() const;
    void copyLocation();
    bool openFolder();

    // ---- Nettoyage ----------------------------------------------------------------------------
    void setCacheChecked(ai::memory::Part p, bool on);
    [[nodiscard]] bool cacheChecked(ai::memory::Part p) const;
    [[nodiscard]] ai::memory::Usage cacheSelection() const;   // ce que Vider le cache liberera
    bool clearCache(bool now = false);
    void setPersistentChecked(ai::memory::Persistent p, bool on);   // une categorie vide ne se coche pas
    [[nodiscard]] bool persistentChecked(ai::memory::Persistent p) const {
        return std::find(persistentOn_.begin(), persistentOn_.end(), p) != persistentOn_.end();
    }
    [[nodiscard]] bool persistentEmpty(ai::memory::Persistent p) const;   // rien a supprimer (0 fichier)
    void setUnderstood(bool on);
    [[nodiscard]] bool understood() const noexcept { return understood_; }
    bool deletePersistent(bool now = false);            // refuse sans "Je comprends"

    // ---- Activite -----------------------------------------------------------------------------
    void setActivityFilter(std::string type);           // "" : tous
    [[nodiscard]] std::size_t activityRowCount() const;

    // ---- la confirmation, le message -----------------------------------------------------------
    [[nodiscard]] bool confirming() const noexcept { return confirm_.kind != Confirm::None; }
    [[nodiscard]] const std::string& confirmText() const noexcept { return confirm_.text; }
    bool confirmYes();
    void confirmNo();
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool messageBad() const noexcept { return messageBad_; }
    // La table de l'onglet courant (scripts : memoire ligne "texte").
    [[nodiscard]] ui::TableView* currentTable() const;
    bool selectRow(std::string_view text);
    void dismiss() { finish(); }

protected:
    core::Status buildUi() override;

private:
    class Body;
    class Rows;
    class OverviewPage;
    class LearningPage;
    class MemoryPage;
    class StoragePage;
    class CleaningPage;
    class ActivityPage;
    class LearningHub;
    friend class Body;
    friend class OverviewPage;
    friend class LearningPage;
    friend class MemoryPage;
    friend class StoragePage;
    friend class CleaningPage;
    friend class ActivityPage;
    friend class LearningHub;

    enum class Confirm : std::uint8_t { None, DeleteKnowledge, DeleteMemory, DeleteCategory, ClearCache, DeletePersistent, ImportLearning };
    struct Pending {
        Confirm     kind{Confirm::None};
        std::string text;
        std::string yes;      // le libelle du bouton Oui
    };
    void ask(Confirm kind, std::string text, std::string yes);
    void say(std::string text, bool bad = false);
    void setLearningSearchQuiet(std::string text);      // la recherche du tableau de bord, pour les connaissances
    void finish();
    void rebuildTables();
    void syncWidgets();
    void fillEditor(const ai::memory::Knowledge* k, bool readOnly);
    bool doDeleteKnowledge();
    bool doDeleteMemory();
    bool doDeleteCategory();
    bool doClearCache();
    bool doDeletePersistent();
    [[nodiscard]] std::string memoryKeyAt(std::size_t modelRow) const;

    Spec                                  spec_;
    ai::memory::Measure                   measure_;
    std::string                           connection_;   // 1.15.0 : l'etat de l'IA (le fournisseur des parametres)
    ai::memory::Ram                       ram_;
    ai::memory::Registry                  registry_;
    std::vector<ai::memory::Conversation> conversations_;
    std::vector<ai::memory::AttachmentFolder> attachments_;
    std::vector<ai::memory::Activity>     activity_;
    std::vector<ai::memory::Hit>          hits_;
    double                                ramAt_{-1.0};

    // Apprentissage
    std::string                           catFilter_, projectFilter_;
    std::string                           learnSearch_;          // 1.17.0 : la recherche du tableau de bord
    std::string                           selectedKnowledge_;
    bool                                  editorReadOnly_{false};
    bool                                  editorAdding_{false};
    // Memoire
    MemCat                                memCat_{MemCat::Conversations};
    std::string                           search_;
    std::string                           selectedMemory_;      // la cle de la ligne choisie
    // Nettoyage
    std::vector<ai::memory::Part>         cacheOn_;
    std::vector<ai::memory::Persistent>   persistentOn_;
    bool                                  understood_{false};
    // Activite
    std::string                           activityFilter_;

    Pending                               confirm_;
    std::string                           message_;
    bool                                  messageBad_{false};
    bool                                  done_{false};
    bool                                  syncing_{false};

    Body*                                 body_{nullptr};
    ui::TabControl*                       tabs_{nullptr};
    OverviewPage*                         overview_{nullptr};
    LearningPage*                         learning_{nullptr};   // la page des connaissances (dans le tableau de bord)
    LearningHub*                          hub_{nullptr};
    MemoryPage*                           memory_{nullptr};
    StoragePage*                          storage_{nullptr};
    CleaningPage*                         cleaning_{nullptr};
    ActivityPage*                         activityPage_{nullptr};
    ui::Button*                           refreshButton_{nullptr};
    ui::Button*                           close_{nullptr};
    ui::Button*                           yes_{nullptr};
    ui::Button*                           no_{nullptr};
    std::shared_ptr<int>                  alive_ = std::make_shared<int>(0);
    core::ConnectionScope                 links_;
};

} // namespace app
