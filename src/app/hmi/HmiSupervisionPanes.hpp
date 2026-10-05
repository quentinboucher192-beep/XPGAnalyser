// =============================================================================
//  app/hmi/HmiSupervisionPanes.hpp — IHM > Configuration : alarmes, recettes,
//  utilisateurs, historiques ; et IHM > Exporter / Importer
// -----------------------------------------------------------------------------
//  ALARMES       le tableau des definitions (nom, priorite, categorie, groupe,
//                condition, message, acquittement, temporisation) et la fiche
//                de l'alarme choisie. Nouvelle, dupliquer, supprimer, ordonner.
//  RECETTES      les recettes, leurs elements (une variable chacun, une unite,
//                des bornes) et leurs jeux de valeurs ; importer / exporter un
//                jeu en CSV, comparer deux jeux.
//  UTILISATEURS  utilisateurs, groupes (operateur, maintenance, superviseur,
//                administrateur), roles et permissions ; mot de passe classique
//                (empreinte salee), code dynamique (le code du moment affiche),
//                autorisation par expression ; la securite en marche ou non.
//  HISTORIQUES   ce qui est garde (alarmes, evenements, systeme, mesures) et
//                ce qui a ete garde : quatre tableaux, export CSV, vider.
//  ECHANGE       exporter tout le projet IHM dans une archive (.zip), importer
//                une archive = reconstruire le projet, puis controler : les
//                comptes de l'archive contre ceux du projet reconstruit, les
//                empreintes des fichiers, et Generer.
//
//  Tout ce qui est projet passe par des commandes (hmi::changeProject) :
//  Ctrl+Z reprend une alarme creee, un mot de passe change, un import entier.
//  L'historique n'est pas une modification (il vient de la marche) : le vider
//  ne s'annule pas, l'hote le fait confirmer.
//
//  Les dialogues (chemins, mots de passe, confirmations) sont a l'hote ; sans
//  hote, les methodes publiques agissent directement (tests, scripts).
// =============================================================================
#pragma once

#include "HmiPanels.hpp"
#include "HmiAskDialog.hpp"            // 1.11 (R111) : Lier... dans une fenetre a cocher
#include "../TablePaste.hpp"
#include "../../core/Command.hpp"
#include "../../hmi/HmiArchive.hpp"
#include "../../hmi/HmiCheck.hpp"
#include "../../hmi/HmiRuntime.hpp"          // lot 13 : ExportRequest (le dossier)
#include "../../hmi/HmiCommands.hpp"
#include "../../hmi/HmiRecipes.hpp"
#include "../../ui/widgets/Containers.hpp"
#include "../../ui/widgets/Controls.hpp"
#include "../../ui/widgets/DataViews.hpp"
#include "../../ui/widgets/SearchField.hpp"   // lot API 8 : chercher (recettes, utilisateurs)

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class HmiGeneratedAlarmsTable;   // 1.9 : HmiObjectAlarmPanes.hpp

// ================================================================= alarmes ===
class HmiAlarmsPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiAlarmsPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    [[nodiscard]] hmi::Id selectedAlarm() const;
    void selectAlarm(hmi::Id);
    // 0 toutes, 1..4 une priorite.
    void setPriorityFilter(int index);
    void setSearch(std::string text);
    [[nodiscard]] std::size_t rowCount() const noexcept { return order_.size(); }

    hmi::Id addAlarm(std::string name = {}, std::string* why = nullptr);
    hmi::Id duplicateAlarm(hmi::Id, std::string* why = nullptr);
    bool    deleteAlarm(hmi::Id);
    bool    moveAlarm(hmi::Id, int delta);
    // nom, condition, message, priorite (1..4 ou "2 - Haute"), categorie, groupe,
    // acquittement, delai (ms), description.
    bool    setField(hmi::Id, const std::string& field, const std::string& value, std::string* why = nullptr);
    // Lot 11 : les reglages de toutes les alarmes - son1..son4 (une ressource
    // son par priorite, "" : aucun), repetition (s), mise_de_cote_max (min).
    bool    setSetting(const std::string& field, const std::string& value, std::string* why = nullptr);

    struct Hosts {
        std::function<void(hmi::Id)> remove;
        // 1.11 (R111) : ouvrir une fenetre qui demande (« Lier... » d'un groupe d'alarmes).
        std::function<void(HmiAskDialog::Spec, std::function<void(bool, const HmiAskDialog::Answer&)>)> ask;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    // 1.11 (R111) : « Lier... » d'un groupe de IHM > Alarmes > Groupes, une fenetre a
    // cocher : les groupes d'objets, les vues (Vue.*), les symboles (symbole:Sym) ; ceux
    // deja lies a ce groupe sont coches. `names` : le nom de chaque case, dans l'ordre.
    [[nodiscard]] HmiAskDialog::Spec linkDialog(const std::string& group, std::vector<std::string>* names = nullptr) const;
    // La reponse de la fenetre : une seule commande (Ctrl+Z). Faux si rien ne change.
    bool applyLinkAnswer(const std::string& group, const std::vector<std::string>& names, const HmiAskDialog::Answer&);
    // La fenetre, par Hosts::ask (la ligne « Lier... » du groupe).
    void openLinkDialog(const std::string& group);
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TableView&    table() noexcept { return *table_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    // 1.9 : les alarmes generees par les objets, sous celles du projet.
    [[nodiscard]] HmiGeneratedAlarmsTable& generated() noexcept { return *generated_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    // 1.11 (R111) : les alarmes nouvelles (Ajouter) dont la priorite n'a pas encore ete
    // choisie : leur groupe, une fois donne, leur donne sa priorite par defaut.
    std::vector<hmi::Id> fresh_;
    HmiGeneratedAlarmsTable* generated_{nullptr};
    HmiToolStrip*     tools_{nullptr};
    ui::DropDown*     priority_{nullptr};
    ui::InputText*    search_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::TableView*    table_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    std::vector<hmi::Id> order_;
    int               priorityFilter_{0};
    std::string       searchText_;
    std::string       message_;
    paste::Binding    paste_;              // lot 20 : coller depuis Excel
    core::ConnectionScope links_;
};

// ================================================================ recettes ===
class HmiRecipesPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiRecipesPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    [[nodiscard]] hmi::Id selectedRecipe() const;
    [[nodiscard]] int     selectedField() const;      // -1 : aucun
    [[nodiscard]] hmi::Id selectedRecord() const;
    void selectRecipe(hmi::Id);
    void selectField(int index);
    void selectRecord(hmi::Id);

    hmi::Id addRecipe(std::string name = {}, std::string* why = nullptr);
    hmi::Id duplicateRecipe(hmi::Id, std::string* why = nullptr);
    bool    deleteRecipe(hmi::Id);
    // nom, description
    bool    setRecipeField(hmi::Id, const std::string& field, const std::string& value, std::string* why = nullptr);

    bool    addField(hmi::Id recipe, hmi::RecipeField f, std::string* why = nullptr);
    bool    removeField(hmi::Id recipe, int index);
    // nom, variable, unite, min, max
    bool    setFieldProp(hmi::Id recipe, int index, const std::string& prop, const std::string& value,
                         std::string* why = nullptr);

    hmi::Id addRecord(hmi::Id recipe, std::string name = {}, std::string* why = nullptr);
    hmi::Id duplicateRecord(hmi::Id recipe, hmi::Id record, std::string* why = nullptr);
    bool    deleteRecord(hmi::Id recipe, hmi::Id record);
    // nom, description
    bool    setRecordProp(hmi::Id recipe, hmi::Id record, const std::string& prop, const std::string& value,
                          std::string* why = nullptr);
    bool    setRecordValue(hmi::Id recipe, hmi::Id record, int field, const std::string& value, std::string* why = nullptr);

    // Un jeu en CSV (Jeu;Element (unite);...), lu depuis / ecrit dans un fichier.
    bool importCsv(hmi::Id recipe, const std::string& path, std::string* why = nullptr);
    bool exportCsv(hmi::Id recipe, const std::string& path, std::string* why = nullptr);
    // Deux jeux cote a cote ; le resultat est dans l'onglet Comparaison.
    std::size_t compare(hmi::Id recipe, hmi::Id left, hmi::Id right);
    [[nodiscard]] const std::vector<hmi::RecipeDiff>& comparison() const noexcept { return diff_; }

    struct Hosts {
        std::function<void(hmi::Id recipe)> importCsv, exportCsv, compare;
        std::function<void(hmi::Id recipe)> remove;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TabControl&   tabs() noexcept { return *tabs_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    // Lot API 8 : chercher une recette (son nom, sa description, ses elements et
    // leurs variables, ses jeux) ; "2 sur 5", retenue d'une seance a l'autre.
    [[nodiscard]] ui::SearchField& search() noexcept { return *search_; }
    [[nodiscard]] std::size_t shownRecipes() const noexcept { return recipeOrder_.size(); }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void refreshDetail();
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::SearchField*  search_{nullptr};     // lot API 8
    ui::Splitter*     split_{nullptr};
    ui::TableView*    recipes_{nullptr};
    ui::Splitter*     centre_{nullptr};
    ui::TableView*    fields_{nullptr};
    ui::TabControl*   tabs_{nullptr};
    ui::TableView*    records_{nullptr};
    ui::TableView*    compare_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> recipesModel_, fieldsModel_, recordsModel_, compareModel_;
    std::vector<hmi::Id> recipeOrder_, recordOrder_;
    hmi::Id           shownRecipe_{hmi::kNoId};
    std::vector<hmi::RecipeDiff> diff_;
    std::string       diffTitle_;
    std::string       message_;
    bool              refreshing_{false};
    core::ConnectionScope links_;
};

// ============================================================ utilisateurs ===
class HmiDynamicCodePanel;

class HmiUsersPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    enum Tab : int { Users = 0, Groups = 1, Roles = 2 };
    HmiUsersPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    void showTab(int tab);
    [[nodiscard]] int currentTab() const;
    [[nodiscard]] hmi::Id     selectedUser() const;
    [[nodiscard]] hmi::Id     selectedGroup() const;
    [[nodiscard]] std::string selectedRole() const;
    void selectUser(hmi::Id);
    void selectGroup(hmi::Id);
    void selectRole(const std::string&);

    // active, depart, periode, chiffres, deconnexion, menu_comptes, menu_acces, menu_journal ;
    // lot 13 : mdp_longueur, mdp_chiffre, mdp_lettre, mdp_casse, mdp_special, mdp_duree,
    // mdp_historique, mdp_premiere, verrou_essais, verrou_minutes, avertir, badge
    bool    setSecurity(const std::string& field, const std::string& value, std::string* why = nullptr);

    hmi::Id addUser(std::string login = {}, std::string group = {}, std::string* why = nullptr);
    bool    deleteUser(hmi::Id);
    // login, nom, groupe, protection, expression, actif, description ; lot 13 : changer
    // (le mot de passe se change a la prochaine connexion)
    bool    setUserField(hmi::Id, const std::string& field, const std::string& value, std::string* why = nullptr);
    // Le mot de passe n'est jamais garde en clair : une empreinte salee.
    bool    setPassword(hmi::Id, const std::string& password, std::string* why = nullptr);
    // Un nouveau secret pour le code dynamique (l'ancien code ne vaut plus).
    bool    newSecret(hmi::Id);
    // Lot 13 : le badge (son numero n'est garde qu'en empreinte ; vide : retire) ;
    // deverrouiller un compte (l'etat des comptes est dans l'historique).
    bool    setBadge(hmi::Id, const std::string& number, std::string* why = nullptr);
    bool    unlockUser(hmi::Id, std::string* why = nullptr);
    [[nodiscard]] std::string codeOf(hmi::Id, double unixSeconds = -1) const;
    // Le secret en base32 (RFC 4648), pour l'application d'authentification.
    [[nodiscard]] std::string secretBase32(hmi::Id) const;

    hmi::Id addGroup(std::string name = {}, int level = 1, std::string* why = nullptr);
    bool    deleteGroup(hmi::Id, std::string* why = nullptr);
    // nom, niveau, roles (a;b), description
    bool    setGroupField(hmi::Id, const std::string& field, const std::string& value, std::string* why = nullptr);

    bool    addRole(std::string name = {}, std::string* why = nullptr);
    bool    deleteRole(const std::string& name, std::string* why = nullptr);
    // nom, description, ou une permission (Naviguer, Piloter...) : TRUE / FALSE
    bool    setRoleField(const std::string& role, const std::string& field, const std::string& value,
                         std::string* why = nullptr);

    struct Hosts {
        std::function<void(hmi::Id user)> password;       // demande le mot de passe (deux fois)
        std::function<void(hmi::Id user)> removeUser;
        std::function<void(hmi::Id group)> removeGroup;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TableView&    usersTable() noexcept { return *users_; }    // lot 20 : coller depuis Excel
    [[nodiscard]] ui::TabControl&   tabs() noexcept { return *tabs_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }
    // Lot API 8 : chercher dans l'onglet montre - un utilisateur (login, nom,
    // groupe, protection, etat, description), un groupe (nom, roles,
    // description), un role (nom, permissions, groupes, description) ;
    // "3 sur 12", retenue d'une seance a l'autre.
    [[nodiscard]] ui::SearchField& search() noexcept { return *search_; }
    [[nodiscard]] std::size_t shownRows() const;      // les lignes montrees de l'onglet choisi

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void say(std::string text, bool warning = false);
    bool change(const std::string& label, const std::function<bool(hmi::Project&, std::string&)>& f, std::string* why);
    void updateSearchCount();                  // lot API 8 : le compte de l'onglet montre
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::SearchField*  search_{nullptr};     // lot API 8
    ui::Splitter*     split_{nullptr};
    ui::TabControl*   tabs_{nullptr};
    ui::TableView*    users_{nullptr};
    ui::TableView*    groups_{nullptr};
    ui::TableView*    roles_{nullptr};
    ui::Splitter*     side_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    HmiDynamicCodePanel* code_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> usersModel_, groupsModel_, rolesModel_;
    std::vector<hmi::Id>     userOrder_, groupOrder_;
    std::vector<std::string> roleOrder_;
    std::string       message_;
    bool              refreshing_{false};
    paste::Binding    paste_;              // lot 20 : coller des utilisateurs depuis Excel
    core::ConnectionScope links_;
};

// ============================================================= historiques ===
class HmiHistoryPane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    // L'ordre des onglets : celui des fichiers ihm/historique/ (lot 13 : l'audit).
    enum Tab : int { Alarms = 0, Events = 1, System = 2, Samples = 3, Audit = 4 };
    HmiHistoryPane(std::string id, hmi::DocumentPtr doc, Apply apply);

    void refresh();
    void showTab(int tab);
    [[nodiscard]] int currentTab() const;
    // alarmes, evenements, systeme (TRUE/FALSE), max, conservation, echantillonnage, archivees (a;b)
    bool setSetting(const std::string& field, const std::string& value, std::string* why = nullptr);
    bool exportCsv(int tab, const std::string& path, std::string* why = nullptr);
    std::size_t clearHistory();
    [[nodiscard]] std::size_t rowsIn(int tab) const;
    // Lot 13 : la chaine du journal d'audit est-elle intacte ? (le message aussi dans la barre)
    hmi::AuditCheck verifyAudit();

    struct Hosts {
        std::function<void(int tab)> exportCsv;
        std::function<void()>        clear;
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&     tools() noexcept { return *tools_; }
    [[nodiscard]] ui::PropertyGrid& properties() noexcept { return *grid_; }
    [[nodiscard]] ui::TabControl&   tabs() noexcept { return *tabs_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildProperties();
    void rebuildTables();
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::Splitter*     split_{nullptr};
    ui::PropertyGrid* grid_{nullptr};
    ui::TabControl*   tabs_{nullptr};
    ui::TableView*    tables_[5]{};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> models_[5];
    std::size_t       shown_[5]{static_cast<std::size_t>(-1), static_cast<std::size_t>(-1), static_cast<std::size_t>(-1),
                                static_cast<std::size_t>(-1), static_cast<std::size_t>(-1)};
    std::string       auditVerdict_;          // lot 13 : la derniere verification
    std::string       message_;
    core::ConnectionScope links_;
};

// ========================================================= exporter / importer ===
class HmiExchangePane final : public ui::Widget {
public:
    using Apply = std::function<void(core::CommandPtr)>;
    HmiExchangePane(std::string id, hmi::DocumentPtr doc, Apply apply, hmi::NameExists plcHasName = {},
                    std::string projectFolder = {});

    // L'archive entiere : configuration, vues, ressources, fichiers externes
    // (le lien), scripts, variables, alarmes, recettes, securite, historiques.
    bool exportTo(const std::string& zipPath, std::string* why = nullptr);
    // Reconstruit le projet depuis l'archive (une commande : Ctrl+Z rend le
    // projet d'avant), reprend son historique, puis controle et lance Generer.
    bool importFrom(const std::string& zipPath, std::string* why = nullptr);
    // Le controle seul, sur le projet tel qu'il est : ses comptes et Generer.
    void check();
    void setNameExists(hmi::NameExists f) { exists_ = std::move(f); }
    void setProjectFolder(std::string f) { folder_ = std::move(f); }

    [[nodiscard]] const hmi::ArchiveReport&     report() const noexcept { return report_; }
    [[nodiscard]] const std::vector<hmi::Issue>& issues() const noexcept { return issues_; }
    [[nodiscard]] const std::string&            lastArchive() const noexcept { return archive_; }
    // "Aucune operation", "Exportee", "Importee", "Controle"
    [[nodiscard]] const std::string&            lastOperation() const noexcept { return operation_; }

    // Lot 13 : le dossier de l'IHM, en Word ou en PDF, ecrit par l'hote (exports/).
    // `renderer` et `theme` : de quoi dessiner les vignettes (nuls : sans vignettes).
    bool makeDossier(gfx::IRenderer* renderer, const ui::Theme* theme, bool pdf, std::string* where = nullptr);
    [[nodiscard]] const std::string& lastDossier() const noexcept { return lastDossier_; }
    [[nodiscard]] std::size_t lastDossierImages() const noexcept { return lastDossierImages_; }

    struct Hosts {
        std::function<void()> exportArchive, importArchive;
        std::function<void(const hmi::Issue&)> openIssue;
        std::function<bool(const hmi::ExportRequest&, std::string*)> writeFile;     // lot 13 : le dossier
    };
    void setHosts(Hosts h) { hosts_ = std::move(h); }
    [[nodiscard]] HmiToolStrip&   tools() noexcept { return *tools_; }
    [[nodiscard]] ui::TabControl& tabs() noexcept { return *tabs_; }
    [[nodiscard]] const std::string& lastMessage() const noexcept { return message_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext&) override;

private:
    void rebuildTables();
    void say(std::string text, bool warning = false);
    hmi::DocumentPtr  doc_;
    Apply             apply_;
    hmi::NameExists   exists_;
    std::string       folder_;
    Hosts             hosts_;
    HmiToolStrip*     tools_{nullptr};
    ui::TabControl*   tabs_{nullptr};
    ui::TableView*    counts_{nullptr};
    ui::TableView*    files_{nullptr};
    ui::TableView*    problems_{nullptr};
    ui::TableView*    issuesTable_{nullptr};
    ui::StatusBar*    status_{nullptr};
    std::shared_ptr<ui::ITableModel> countsModel_, filesModel_, problemsModel_, issuesModel_;
    hmi::ArchiveReport       report_;
    std::vector<hmi::Issue>  issues_;
    std::string       archive_, operation_{"Aucune op\xC3\xA9ration"}, stamp_;
    std::string       message_;
    int               pendingDossier_{0};          // lot 13 : 1 Word, 2 PDF, au prochain dessin
    std::string       lastDossier_;
    std::size_t       lastDossierImages_{0};
    core::ConnectionScope links_;
};

} // namespace app
