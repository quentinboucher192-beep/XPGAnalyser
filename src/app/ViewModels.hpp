// =============================================================================
//  app/ViewModels.hpp - the M-V seam
// -----------------------------------------------------------------------------
//  These adapters are the whole of the "controller" in this MVC arrangement:
//  they present a domain::Project through the ITableModel / ITreeModel
//  interfaces the widgets consume. Nothing in domain/ knows they exist, and
//  nothing in ui/ knows what a PLC is.
//
//  Every adapter holds a shared_ptr<const Project>. Sharing rather than
//  referencing means a background re-import can build a new Project while the
//  old one is still on screen; the swap is one atomic pointer exchange at a
//  frame boundary, with no window of inconsistency.
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "../import/ProjectAnalyzer.hpp"

#include <optional>
#include "../ui/widgets/DataViews.hpp"
#include "../domain/ExecutionOrder.hpp"
#include "../project/MemberTree.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hmi { class Document; }
namespace hmi::build { class Cache; }          // 1.11 (chantier T3, C4) : hmi/HmiBuildState.hpp
namespace app::alarmtree { struct Group; }   // 1.10 (chantier O) : hmi/HmiObjectAlarmTree.hpp

namespace app {

    using ProjectRef = std::shared_ptr<const domain::Project>;

    // ------------------------------------------------- variables (TableView) ----
    // Columns from the brief: Name, Type, Address, Scope, Comment, Usage.
    class VariableTableModel final : public ui::ITableModel {
    public:
        enum Column : std::size_t { Name, Type, Address, Scope, Comment, Usage, ColumnCount };

        VariableTableModel(ProjectRef project, importer::ProjectAnalyzer::ReferenceIndex refs);

        [[nodiscard]] std::size_t rowCount() const override;
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] bool less(ui::RowIndex, ui::RowIndex, std::size_t) const override;

        // Exposed so the "Show: All / Unused / Located / Global" dropdown can build
        // predicates without reaching into the project.
        [[nodiscard]] const domain::Variable& variable(ui::RowIndex) const;
        [[nodiscard]] std::uint32_t usage(ui::RowIndex) const;

    private:
        ProjectRef                                     project_;
        importer::ProjectAnalyzer::ReferenceIndex      refs_;
    };

    // --------------------------------------------- project explorer (TreeView) ---
    // One tree, several sub-roots, matching the left pane of the mockup:
    //
    //   <project>
    //     ?? Configuration        (station, CPU, tasks)
    //     ?? Derived data types
    //     ?? DFB types
    //     ?? Program units        (les unites seules : une section de tache est sous Tasks)
    //     ?    ?? <unit> ? Entrees, Sorties, E/S, publiques, privees (non vides), puis <section>
    //     ?? Ordre d'execution    (les sections de la tache, numerotees, dans l'ordre)
    //     ?? Tasks ? MAST ? <section in execution order>
    //     ?? Animation tables
    //
    // "Ordre d'execution" est un dossier a part et non un tri de "Program units",
    // parce que ce n'est pas la meme question. "Program units" repond a "qu'est-ce
    // qu'il y a dans ce projet" ; celui-ci repond a "dans quel ordre ca tourne",
    // et il mele des sections qui vivent a deux endroits differents du modele.
    //
    // NodeId packs a kind and an index, so no node objects are allocated: a 10 000
    // POU tree costs nothing until it is expanded.
    class ProjectTreeModel final : public ui::ITreeModel {
    public:
        enum class NodeKind : std::uint8_t {
            Root, ConfigurationFolder, Cpu, RackFolder, Rack, HwModule, TaskFolder, Task,
            TypesFolder, DerivedType, DerivedField,
            DfbFolder, DfbType, DfbSection,
            // A DFB type is not just its code: its interface is what a reviewer
            // actually needs. These five folders expose the declarations that were
            // previously parsed but never surfaced.
            DfbInputs, DfbOutputs, DfbInOut, DfbPublicVars, DfbPrivateVars,
            DfbSectionsFolder, DfbVariable,
            UnitsFolder, ProgramUnit, ProgramUnitVars, Section,
            // L'ordre d'execution, en clair. `Section::order` etait lu, ecrit et
            // range depuis le debut, et l'arbre ne le montrait nulle part : on ne
            // pouvait donc ni le verifier ni le changer. Une etape porte le rang de
            // la tache dans son index et le rang de l'etape dans son sous-index.
            ExecOrderFolder, ExecOrderTask, ExecStep,
            TablesFolder, AnimationTable,

            // LES TROIS LISTES A PLAT, LES SOUS-ROUTINES ET LES MACROS.
            //
            //  L'arbre suit la structure du projet : pour trouver toutes les
            //  instances d'un DFB il fallait ouvrir chaque unite l'une apres
            //  l'autre. Ces dossiers repondent a l'autre question - "qu'est-ce
            //  qu'il y a, de ce genre-la, dans tout le projet" - et ce sont des
            //  listes plates parce que la reponse n'a pas de hierarchie.
            //
            //  Elles sont CALCULEES ET GARDEES : les recalculer a chaque
            //  childCount() ferait trois parcours de toutes les variables par
            //  image. `refresh()` les refait apres une macro, qui vient justement
            //  de changer ce dont elles sont un instantane.
            //  Une feuille de ces listes porte un GENRE A ELLE (`ListVariable`) et
            //  non celui de son dossier : sans ca, `childCount` d'une feuille rend
            //  la taille de la liste et l'arbre se deplie a l'infini. Son index est
            //  le RANG dans la liste, son sous-index dit DANS LAQUELLE.
            VariablesFolder, ElementaryFolder, DdtInstanceFolder, DfbInstanceFolder,
            ListVariable,
            SubroutinesFolder, Subroutine,
            MacroFolder, Macro,

            // L'IHM. Un dossier a elle, dans l'ordre de la specification :
            // Configuration, Fichiers externes, Ressources, Exporter / Importer,
            // Vues, Simulation, Generer, Compiler, Programmation generale.
            // Une vue porte son RANG dans le projet IHM ; ses cinq parties
            // (objets, scripts, animations, calques, groupes) sont des feuilles
            // qui ouvrent l'editeur de la vue - sous-index = la partie.
            // AJOUTES A LA FIN : le genre est range dans le NodeId, et
            // renumeroter les anciens changerait l'identite de noeuds deplies.
            HmiFolder, HmiConfig, HmiExternalFiles, HmiResources, HmiExchange,
            HmiViews, HmiView, HmiViewPart, HmiSimulation, HmiGenerate, HmiCompile, HmiScripts,
            // Lot 4 : les quatre sous-noeuds de Configuration (alarmes, recettes,
            // utilisateurs, historiques). A LA FIN, pour la meme raison.
            HmiAlarms, HmiRecipes, HmiUsers, HmiHistory,
            // Lot 5 : ce qui se deballe. A LA FIN, toujours.
            //   HmiObject        index = vue, sub = objet (ses surcharges, ses membres)
            //   HmiObjectItem    index = vue, sub = (objet << 8) | rang de la surcharge
            //   HmiViewScript    index = vue, sub = rang du script de la vue
            //   HmiAnimation     index = vue, sub = rang de la propriete animee
            //   HmiLayer         index = vue, sub = rang du calque
            //   HmiGroupEntry    index = vue, sub = groupe (sous "Groupes" : une feuille)
            //   HmiAlarmGroup    index = rang du groupe (alphabetique, "sans groupe" a la fin)
            //   HmiAlarm         index = alarme
            //   HmiRecipe        index = recette ; HmiRecord : index = recette, sub = jeu
            //   HmiUserGroup     index = groupe ; HmiUser : index = utilisateur
            //   HmiRoles         le dossier des roles ; HmiRole : index = rang du role
            //   HmiScriptsFolder, HmiVariablesFolder, HmiUsedFolder : les trois
            //   dossiers de la Programmation generale ; HmiGeneralScript : index =
            //   script ; HmiVariable : index = variable IHM ; HmiUsedVariable :
            //   index = rang dans la liste des variables employees.
            HmiObject, HmiObjectItem, HmiViewScript, HmiAnimation, HmiLayer, HmiGroupEntry,
            HmiAlarmGroup, HmiAlarm, HmiRecipe, HmiRecord, HmiUserGroup, HmiUser, HmiRoles, HmiRole,
            HmiScriptsFolder, HmiVariablesFolder, HmiUsedFolder, HmiGeneralScript, HmiVariable, HmiUsedVariable,
            // Lot 7 : les fonctions IHM (Programmation generale > Fonctions ;
            // HmiFunction : index = fonction). A LA FIN, toujours.
            HmiFunctionsFolder, HmiFunction,
            // Lot 8 : les vues rangees par role. Vues > Modeles (ecrans modeles,
            // en-tetes, pieds de page), Vues, Popups. HmiViewFolder : index =
            // hmi::ViewFolder ; HmiTemplateFolder : index = 0 ecrans modeles,
            // 1 en-tetes, 2 pieds. A LA FIN, toujours.
            HmiViewFolder, HmiTemplateFolder,
            // Lot 9 : les variables systeme et d'instances (Programmation generale).
            //   HmiSysFolder   ses domaines ; HmiSysDomain : index = rang du domaine ;
            //   HmiSysVar      index = rang dans hmi::pub::kSysVars ;
            //   HmiInstFolder  les vues ; HmiInstView : index = vue (ses variables
            //                  dans un dossier a elles, puis ses objets) ;
            //   HmiInstViewVar index = vue, sub = rang dans hmi::pub::kViewInfo ;
            //   HmiInstObject  index = vue, sub = objet ;
            //   HmiInstVar     index = vue, sub = (objet << 8) | rang de la variable.
            // A LA FIN, toujours.
            HmiSysFolder, HmiSysDomain, HmiSysVar, HmiInstFolder, HmiInstView, HmiInstViewVar, HmiInstObject, HmiInstVar,
            //   HmiInstViewInfo "Variables de la vue" (index = vue) : ses HmiInstViewVar.
            HmiInstViewInfo,
            // Lot 10 : l'automate a son dossier, a cote de celui de l'IHM. Avec
            // l'IHM, la racine porte API et IHM ; API porte les dix dossiers de
            // l'automate (leurs NodeId ne changent pas : ce qui etait deplie le
            // reste). Sans IHM, la racine garde ses dix dossiers. A LA FIN.
            ApiFolder,
            // Lot 10 : IHM > Symboles (apres Vues) - les vues de role symbole, des
            // HmiView comme les autres (elles s'ouvrent dans l'editeur). A LA FIN.
            HmiSymbolsFolder,
            // Lot 12 : IHM > Styles (les styles nommes) et IHM > Rechercher /
            // remplacer - deux volets, pas des dossiers. A LA FIN.
            HmiStyles, HmiFind,
            // Lot 13 : IHM > Essais (les essais de reception) - un volet. A LA FIN.
            HmiTests,
            // Lot 13 : Configuration > Langues (les langues et les traductions) et
            // Configuration > Unites et formats (ceux des variables) - deux volets,
            // les cinquieme et sixieme sous-noeuds de Configuration. A LA FIN.
            HmiLanguages, HmiUnits,
            // Lot 14 : Configuration > Communication (l'automate reel, Modbus TCP) -
            // le septieme sous-noeud de Configuration. A LA FIN.
            HmiComm,
            // Lot 14 : Configuration > Poste d'exploitation (l'IHM seule, en plein
            // ecran) - le huitieme sous-noeud de Configuration. A LA FIN.
            HmiStation,
            // Lot 14 : Configuration > Notifications, Rapports, Acces web - les
            // neuvieme, dixieme et onzieme sous-noeuds de Configuration. A LA FIN.
            HmiNotify, HmiReports, HmiWeb,
            // Lot 15 : l'outil Modbus (lire, ecrire, trames, espion, ping) - un
            // noeud du dossier IHM, apres Simulation. A LA FIN.
            HmiModbusTool,
            // Lot 16 : Programmation generale > Types IHM (HmiTypeNode : index =
            // type) ; les dossiers des variables IHM (HmiVarFolder : index = rang
            // dans hmi::types::allFolders). A LA FIN.
            HmiTypesFolder, HmiTypeNode, HmiVarFolder,
            // Lot 21 : les versions du projet, a la racine a cote de API et IHM.
            // VersionItem : index = rang dans la liste (0 : le travail en cours,
            // puis la plus recente d'abord). A LA FIN.
            VersionsFolder, VersionItem,
            // Lot 21 : un dossier d'une liste de l'IHM (vues, popups, modeles,
            // en-tetes, pieds, symboles, scripts, types) : index = rang du dossier
            // dans hmi::fold::allFolders de sa liste, sub = la liste. A LA FIN.
            HmiListFolder,
            // Lot macros 1 : un dossier de macros (libs/Macros/dossiers.txt) :
            // index = rang du dossier dans la liste donnee a setMacroTree. A LA FIN.
            MacroSubFolder,
            // Lot API 4 : Configuration > Voies et adresses, Reseau, Plan memoire
            // (des onglets, pas des dossiers). A LA FIN.
            ApiChannels, ApiNetwork, ApiMemory,
            // Lot API 7 : API > Simulation et API > Statistiques (des onglets,
            // l'ancien ecran F9 et l'ancien Ctrl+5). A LA FIN.
            ApiSimulation, ApiStatistics,
            // Lot API 7 : LES MEMBRES, A TOUTE PROFONDEUR. Une variable dont le
            // type a des membres (un DDT, un tableau, une instance de DFB ou de
            // bloc) se deplie - DfbVariable, DerivedField, ListVariable - puis
            // chacun de ses membres a son tour (project/MemberTree) : les champs,
            // les broches, les elements, par paquets de 100 au-dela de 100.
            // MemberNode : index = rang dans la table des membres du modele,
            // remplie a mesure qu'on deplie. A LA FIN.
            MemberNode,
            // ---- Lot API 8 : Centre de simulation ----
            // Le dossier Simulation, a la racine entre IHM et Versions : Vue
            // d'ensemble, Automate (ApiSimulation, venu de l'API), IHM
            // (HmiSimulation, venu de l'IHM), Equipements, Debogage, Forcages,
            // Courbes, Journal. A LA FIN, toujours.
            SimFolder, SimOverview, SimEquipment, SimDebug, SimForcing, SimTrends, SimJournal,
            // ---- fin Lot API 8 ----
            // ---- Lot API 8 : l'arbre du projet ----
            // Un resultat du filtre de l'arbre trouve DANS le contenu (Aller a...),
            // sous le dossier de son domaine : index = rang dans filterHits().
            // Il n'existe que pendant un filtre. A LA FIN, toujours.
            FilterHit,
            // Les outils sortis de l'arbre : la premiere ligne du dossier API
            // (index 0 : Statistiques) et du dossier IHM (index 1 : Echanges,
            // Rechercher, Outil Modbus, Generer, Compiler), des boutons.
            ToolRow,
            // Les versions en bref : apres le travail en cours et les 5 dernieres,
            // "Voir les 47 versions..." (un clic les montre toutes).
            VersionsMore,
            // Epingles et Recents, en haut de la racine : des raccourcis (index =
            // rang dans la liste), chacun vers un autre noeud (shortcutTarget).
            PinsFolder, PinItem, RecentFolder, RecentItem,
            // ---- fin Lot API 8 : l'arbre du projet ----
            // ---- 1.10 (chantier O) : DEPLIER UN OBJET MONTRE SES ALARMES ----
            //   HmiObjectAlarms  "Alarmes . <groupe> (n)" sous l'objet (le dernier
            //                    de ses enfants) : index = vue, sub = objet ;
            //   HmiObjectAlarm   une ligne de ce noeud (une alarme, ou le groupe
            //                    d'un objet d'une instance de symbole) : index =
            //                    vue, sub = (objet << 8) | rang dans la liste a plat.
            // A LA FIN, toujours.
            HmiObjectAlarms, HmiObjectAlarm,
            // ---- 1.10 (chantier O, decisions 14 et 15) : SOUS UN TYPE IHM ----
            //   HmiTypeValues    "Valeurs (n)" d'une enumeration : index = type (id & kMask28) ;
            //   HmiTypeValue     "Arret = 0" (le texte affiche a droite) : sub = rang ;
            //   HmiTypeOperators "Operateurs (n)" (apres les valeurs) ; HmiTypeOperator
            //                    une signature (toString / fromString a droite) : sub = rang.
            // A LA FIN, toujours.
            HmiTypeValues, HmiTypeValue, HmiTypeOperators, HmiTypeOperator,
            // ---- 1.10 (chantier O, decision 14) : sous un objet, APRES Alarmes, les
            //   operateurs de son symbole : HmiObjectOperators (index = vue, sub = objet),
            //   HmiObjectOperator (sub = (objet << 8) | rang, comme HmiObjectAlarm).
            HmiObjectOperators, HmiObjectOperator,
            // ---- 1.10.2 (chantier A) : L'OBJET DEPLIE PAR FAMILLES (maquette M13, scene 5) ----
            //   HmiObjectFamily  "Actions [2]", "Liens fx [4]"... : index = vue,
            //                    sub = (objet << 8) | famille (hmitree::Family) ;
            //   HmiObjectParam   un parametre d'une instance : sub = (objet << 8) | rang ;
            //   HmiObjectMarker  un repere $Nom$ de l'objet : sub = (objet << 8) | rang.
            // A LA FIN, toujours.
            HmiObjectFamily, HmiObjectParam, HmiObjectMarker,
            // ---- 1.11.1 (decision 108) : VARIABLES D'INSTANCES, TOUT CE QU'UN OBJET PUBLIE ----
            //   sous HmiInstObject, apres ses variables (HmiInstVar), comme le volet :
            //   HmiInstParam      un parametre d'une instance (Vue.Pompe_3.Armoire) : index =
            //                     vue, sub = (objet << 8) | rang dans hmi::pub::instanceParams ;
            //   HmiInstAlarmGroup "Groupe d'alarmes Vue.Objet -> <groupe lie>" : index = vue,
            //                     sub = objet ; HmiInstGroupVar : une de ses variables,
            //                     sub = (objet << 8) | rang dans hmi::pub::kAlarmInfo ;
            //   HmiInstAlarms     "Alarmes" : index = vue, sub = objet ; HmiInstAlarm : une
            //                     alarme, sub = (objet << 8) | rang dans hmi::pub::objectAlarmNames ;
            //   HmiInstAlarmVar   un de ses membres (Vue.Objet.Alarmes.<alarme>.<membre>) :
            //                     index = (rang dans hmi::pub::kAlarmMembers << 24) | vue,
            //                     sub = celui de l'alarme.
            // A LA FIN, toujours.
            HmiInstParam, HmiInstAlarmGroup, HmiInstGroupVar, HmiInstAlarms, HmiInstAlarm, HmiInstAlarmVar,
        };
        // Les cinq parties d'une vue, dans l'ordre de l'arbre.
        enum class HmiPart : std::uint8_t { Objects, Scripts, Animations, Layers, Groups, Count };

        explicit ProjectTreeModel(ProjectRef project);

        [[nodiscard]] ui::NodeId    root() const override;
        [[nodiscard]] std::size_t   childCount(ui::NodeId) const override;
        [[nodiscard]] ui::NodeId    childAt(ui::NodeId, std::size_t) const override;
        [[nodiscard]] bool          hasChildren(ui::NodeId) const override;
        [[nodiscard]] std::string   text(ui::NodeId) const override;
        [[nodiscard]] ui::CellStyle style(ui::NodeId) const override;

        static ui::NodeId    pack(NodeKind, domain::Index, domain::Index sub = domain::kNoIndex);
        static NodeKind      kindOf(ui::NodeId) noexcept;
        static domain::Index indexOf(ui::NodeId) noexcept;
        static domain::Index subOf(ui::NodeId) noexcept;

        // La section derriere un noeud, quel qu'il soit : une etape de l'ordre
        // d'execution, une section de tache, une section d'unite. Rend kNoIndex
        // pour tout le reste. C'est ce dont le menu contextuel et le
        // glisser-deposer ont besoin, et ca leur evite de connaitre le paquetage.
        [[nodiscard]] domain::Index sectionOf(ui::NodeId) const;

        // Lot API 7 : l'etat de la simulation, en pastille sur API > Simulation
        // ("en marche", "halte") ; vide : pas de pastille.
        void setSimulationBadge(std::string text, ui::Tone tone) { simBadge_ = std::move(text); simTone_ = tone; }

        // La variable derriere un noeud des listes a plat, et la SECTION derriere
        // un noeud de sous-routine. kNoIndex pour tout le reste : l'appelant teste
        // le resultat plutot que le genre du noeud, et n'a donc pas a connaitre
        // les genres qui n'existaient pas quand il a ete ecrit.
        [[nodiscard]] domain::Index variableOf(ui::NodeId) const;
        [[nodiscard]] domain::Index subroutineOf(ui::NodeId) const;

        // Lot API 7 : LES MEMBRES DEPLIES. Le chemin du membre derriere un noeud
        // MemberNode ("armoires[0].sorties.V3" ; un paquet : "tempon[100 ... 149]"),
        // vide pour tout le reste. Il part de sa racine : une variable globale
        // (le nom que la simulation connait), une variable d'unite ou de DFB
        // (sans le nom de l'unite), un champ de DDT (sans le nom du DDT).
        [[nodiscard]] std::string memberPathOf(ui::NodeId) const;
        // Sa cle project/MemberTree ("armoires[0].sorties", un paquet :
        // "tempon[100..149]", une ligne : "Grille[2,*]"), depuis la meme racine
        // que memberPathOf : ce que VariablesPane::setExpanded comprend, et les
        // volets des types une fois prefixee ("m:<Type|Bloc|Unite>.<cle>").
        // Vide pour tout le reste.
        [[nodiscard]] std::string memberKeyOf(ui::NodeId) const;
        // Un paquet, une ligne d'un tableau : ils rangent des elements et n'ont
        // pas de valeur a eux (pas de table d'animation, pas de forcage). Faux
        // pour un membre reel et pour tout le reste.
        [[nodiscard]] bool memberIsGroup(ui::NodeId) const;
        // Le noeud qui a deplie ce membre : sa variable (DfbVariable,
        // DerivedField, ListVariable) ou un autre MemberNode ; kInvalidNode pour
        // tout le reste. Montrer une feuille, c'est deplier son parent.
        [[nodiscard]] ui::NodeId memberParentOf(ui::NodeId) const;
        // La variable racine d'un membre (la globale, la variable de l'unite ou
        // du DFB, le champ du DDT) ; kNoIndex pour tout le reste - variableOf,
        // lui, ne designe jamais un membre.
        [[nodiscard]] domain::Index memberRootOf(ui::NodeId) const;
        // Un noeud dont les enfants, s'il en a, sont des membres : une variable
        // (DfbVariable, DerivedField, ListVariable) ou un membre. "Tout deplier"
        // doit s'y arreter - un tableau de 10 000 structures ferait des
        // centaines de milliers de lignes.
        [[nodiscard]] static bool holdsMembers(ui::NodeId) noexcept;

        // Les macros disponibles, rangees par SharedLibrary. Le modele ne va pas
        // les chercher : il ne connait pas le disque, et l'ecran qui scanne libs/
        // le fait deja pour la liste des macros.
        void setMacros(std::vector<std::string> names);
        [[nodiscard]] const std::vector<std::string>& macros() const noexcept { return macros_; }
        // Lot macros 1 : LES MACROS RANGEES EN DOSSIERS, comme dans l'onglet
        // Macros. `folders` : tous les chemins ("A", "A/B"), parents d'abord ;
        // `macros` : (nom, dossier) dans l'ordre d'affichage.
        void setMacroTree(std::vector<std::string> folders, std::vector<std::pair<std::string, std::string>> macros);
        // Le chemin d'un dossier de macros ("" pour le dossier Macros lui-meme) ;
        // faux si le noeud n'est pas un dossier de macros.
        [[nodiscard]] bool macroFolderOf(ui::NodeId, std::string& path) const;
        // Le noeud d'un dossier de macros, d'une macro (kInvalidNode : inconnu).
        [[nodiscard]] ui::NodeId macroFolderNode(const std::string& path) const;
        [[nodiscard]] ui::NodeId macroNode(const std::string& name) const;

        // RECALCULER LES LISTES A PLAT. A appeler apres tout ce qui ajoute ou
        // enleve des variables - une macro, surtout, qui en cree cinquante d'un
        // coup. Sans ca l'arbre affiche l'etat d'avant et personne ne comprend
        // pourquoi les instances creees ne sont pas la.
        void refresh();

        // LE PROJET IHM. Sans lui (nullptr), le dossier IHM n'apparait pas et
        // la racine garde ses dix dossiers : les tests qui ne connaissent pas
        // l'IHM voient l'arbre d'avant. Avec lui (lot 10), la racine porte deux
        // dossiers au meme niveau : API (les dix dossiers de l'automate) et IHM. Le modele ne garde qu'un pointeur :
        // renommer une vue se voit au prochain dessin, sans rien refaire.
        void setHmi(std::shared_ptr<const hmi::Document> hmi);
        [[nodiscard]] bool hasHmi() const noexcept { return hmi_ != nullptr; }
        // La structure du projet IHM a change (vue ajoutee, retiree...) :
        // l'arbre refait ses lignes, sans rien replier.
        void hmiChanged();
        // L'identifiant de la vue derriere un noeud Vue ou partie de vue ;
        // 0 (hmi::kNoId) pour tout le reste.
        [[nodiscard]] std::uint64_t hmiViewOf(ui::NodeId) const;
        [[nodiscard]] static ui::NodeId hmiFolderNode() { return pack(NodeKind::HmiFolder, 0); }
        // Lot 10 : le dossier API (avec l'IHM ; sans elle, la racine en tient lieu).
        [[nodiscard]] static ui::NodeId apiFolderNode() { return pack(NodeKind::ApiFolder, 0); }
        [[nodiscard]] ui::NodeId hmiViewNode(std::uint64_t viewId) const;
        // Ce que designe un noeud deballe (lot 5), pour l'ouvrir au bon endroit :
        // l'objet (HmiObject, HmiObjectItem, HmiAnimation, HmiGroupEntry), le rang
        // (surcharge, script de vue, animation, calque, role, variable employee),
        // l'identifiant (alarme, recette, utilisateur, groupe, script, variable)
        // et, pour un jeu de recette, le jeu. 0 / -1 : rien.
        [[nodiscard]] std::uint64_t hmiObjectOf(ui::NodeId) const;
        [[nodiscard]] int           hmiRankOf(ui::NodeId) const;
        [[nodiscard]] std::uint64_t hmiIdOf(ui::NodeId) const;
        [[nodiscard]] std::uint64_t hmiRecordOf(ui::NodeId) const;
        // Le chemin de la variable employee derriere un noeud HmiUsedVariable.
        [[nodiscard]] std::string   hmiUsedPathOf(ui::NodeId) const;
        // 1.11.1 (decision 108) : le chemin d'une variable de « Variables d'instances »
        // (Vue.Variable, Vue.Objet.Membre, Vue.Pompe_3.Armoire, Vue.Objet.AlarmGroup,
        // Vue.Objet.Alarmes.Defaut.Acked) ; "" pour un dossier ou un autre noeud.
        [[nodiscard]] std::string   hmiInstPathOf(ui::NodeId) const;
        // Le debut commun des chemins sous un noeud de « Variables d'instances »
        // ("Vue.", "Vue.Objet.", "Vue.Objet.Alarmes.Defaut.") : le filtre du volet.
        [[nodiscard]] std::string   hmiInstPrefixOf(ui::NodeId) const;
        // ---- 1.10 (chantier O) : les alarmes d'un objet (HmiObjectAlarms) ----
        // Le modele ne lie pas la bibliotheque de l'IHM : l'ecran lui donne de
        // quoi les trouver (vue, objet -> le noeud ; nul : pas d'alarme). Sans
        // fournisseur, pas de noeud. Gardees jusqu'au prochain changement.
        using ObjectAlarms = std::function<std::shared_ptr<const alarmtree::Group>(std::uint64_t view, std::uint64_t object)>;
        void setObjectAlarms(ObjectAlarms fn);
        [[nodiscard]] std::shared_ptr<const alarmtree::Group> objectAlarms(std::uint64_t view, std::uint64_t object) const;
        // L'alarme derriere un noeud HmiObjectAlarm : son nom dans le symbole (ou
        // la bibliotheque) et son chemin dans le symbole ; faux pour le reste.
        [[nodiscard]] bool hmiObjectAlarmOf(ui::NodeId, std::string& name, std::string& path) const;
        // Lot 16 : le dossier de variables IHM d'un noeud ("Ligne/Convoyeur") ; vide sinon.
        [[nodiscard]] std::string   hmiFolderOf(ui::NodeId) const;
        // Lot 21 : le texte d'un dossier d'une liste IHM ("Armoires  [3]").
        [[nodiscard]] std::string   hmiListFolderText(ui::NodeId) const;
        // Lot 21 : LES LISTES RANGEES EN DOSSIERS (hmi::fold::List, en int pour ne
        // pas tirer l'IHM dans cet en-tete). Un dossier (HmiListFolder) ou le
        // noeud d'une liste (Vues, Popups, un modele, Symboles, Scripts, Types
        // IHM - le dossier "" : sa racine) : vrai, sa liste et son chemin.
        [[nodiscard]] bool          hmiListFolderOf(ui::NodeId, int& list, std::string& folder) const;
        // Un element d'une de ces listes (une vue, un script, un type) : sa liste ; -1 sinon.
        [[nodiscard]] int           hmiListOf(ui::NodeId) const;
        // Le noeud d'un dossier d'une liste (kInvalidNode : inconnu).
        [[nodiscard]] ui::NodeId    hmiListFolderNode(int list, const std::string& folder) const;

        // Lot 21 : les versions du projet (Versions, a la racine avec l'IHM).
        // La premiere ligne : le travail en cours ; puis la plus recente d'abord.
        struct VersionRow {
            int         number{0};      // 0 : le travail en cours
            std::string label;          // "V5 - Livree au client", "Travail en cours - modifie"
            ui::Tone    tone{ui::Tone::Muted};
            std::string badge;          // "3" (changements), vide sinon
        };
        void setVersions(std::vector<VersionRow> rows);
        [[nodiscard]] const std::vector<VersionRow>& versions() const noexcept { return versions_; }
        [[nodiscard]] static ui::NodeId versionsFolderNode() { return pack(NodeKind::VersionsFolder, 0); }
        // Le numero de version derriere un noeud VersionItem (0 : le travail en cours) ; -1 sinon.
        [[nodiscard]] int versionOf(ui::NodeId) const;

        // ---- Lot API 8 : Centre de simulation ----
        //  Avec l'IHM, la racine porte API, IHM, Simulation, Versions ; le dossier
        //  Simulation porte Vue d'ensemble, Automate, IHM, Equipements, Debogage,
        //  Forcages, Courbes, Journal. API > Simulation et IHM > Simulation y sont
        //  venus (memes genres, memes NodeId : Automate et IHM).
        [[nodiscard]] static ui::NodeId simFolderNode() { return pack(NodeKind::SimFolder, 0); }
        // Un noeud du dossier Simulation (le dossier compris, Automate et IHM compris).
        [[nodiscard]] static bool isSimNode(ui::NodeId) noexcept;
        // La pastille d'un noeud du dossier Simulation (le dossier lui-meme, IHM,
        // Equipements, Forcages, Courbes, Journal) ; vide : pas de pastille.
        // Automate garde la sienne (setSimulationBadge). Vrai : elle a change.
        bool setSimBadge(NodeKind kind, std::string text, ui::Tone tone);
        // ---- fin Lot API 8 ----

        // ---- Lot API 8 : l'arbre du projet ----
        //  Les resultats du filtre trouves DANS le contenu (Aller a...) : des
        //  noeuds FilterHit a la fin du dossier de leur domaine (API ou IHM ;
        //  sans IHM, a la fin de la racine). Vide : plus de FilterHit.
        struct FilterHitRow {
            std::string title, hint;             // "SFC_PurgeA", "Section - Logigrammes_A"
            ui::Icon    icon{ui::Icon::None};
            bool        hmi{false};              // sous IHM (sinon sous API)
        };
        void setFilterHits(std::vector<FilterHitRow> hits);
        [[nodiscard]] const std::vector<FilterHitRow>& filterHits() const noexcept { return filterHits_; }
        //  Les outils sortis de l'arbre (avec l'IHM) : la ligne ToolRow en tete
        //  du dossier API et du dossier IHM porte des boutons (CellStyle::chips) ;
        //  le bouton k ouvre ce qu'ouvrait le noeud toolNode(hmi, k), qui n'est
        //  plus dans l'arbre (Statistiques ; Exporter / Importer, Rechercher /
        //  remplacer, Outil Modbus, Generer, Compiler).
        [[nodiscard]] static std::size_t toolCount(bool hmi) noexcept { return hmi ? 5 : 1; }
        [[nodiscard]] static ui::NodeId  toolNode(bool hmi, std::size_t k);
        [[nodiscard]] static ui::NodeId  toolRowNode(bool hmi) { return pack(NodeKind::ToolRow, hmi ? 1 : 0); }
        //  La pastille rouge (Tone::Error) : les expressions impossibles du dernier
        //  Generer / Compiler, sur IHM > Vues ("2") et sur le titre IHM ("2 erreurs").
        //  Vrai : le nombre a change.
        //  inViews : celles qui sont dans une vue (la pastille de Vues ; les autres sont
        //  dans les alarmes, les scripts generaux...). Sans lui : toutes.
        bool setHmiExprErrors(std::size_t n, std::size_t inViews);
        bool setHmiExprErrors(std::size_t n) { return setHmiExprErrors(n, n); }
        [[nodiscard]] std::size_t hmiExprErrors() const noexcept { return hmiExprErrors_; }
        [[nodiscard]] std::size_t hmiExprErrorsInViews() const noexcept { return hmiExprErrorsInViews_; }
        //  1.11 (chantier T3, C4) : l'etat de Compiler et de Generer, lu dans un
        //  cache (hmi::build::Cache, refait au chargement, a Compiler et a la
        //  revision d'un script ; jamais a chaque image). Les icones a droite des
        //  sections, des types DFB et des scripts de l'IHM (CellStyle::trail).
        //  nullptr : pas d'icone.
        void setBuildState(std::shared_ptr<const hmi::build::Cache> cache) { buildState_ = std::move(cache); }
        [[nodiscard]] const hmi::build::Cache* buildState() const noexcept { return buildState_.get(); }
        //  Pour les deux filtres de l'arbre : 1 ne compile pas (✕ ou ⊘), 2 n'est pas
        //  genere (un script) ; 0 : rien a signaler, ou pas d'etat pour ce noeud.
        enum : std::uint8_t { BuildNotCompiling = 1, BuildNotGenerated = 2 };
        [[nodiscard]] std::uint8_t buildFlags(ui::NodeId) const;
        //  Les versions en bref : le dossier Versions montre le travail en cours
        //  et les kBriefVersions dernieres, puis VersionsMore ; setAllVersions(true) :
        //  toutes (comme avant). Vrai : ca a change.
        static constexpr std::size_t kBriefVersions = 5;
        bool setAllVersions(bool all);
        [[nodiscard]] bool allVersions() const noexcept { return allVersions_; }
        [[nodiscard]] static ui::NodeId versionsMoreNode() { return pack(NodeKind::VersionsMore, 0); }
        // Pendant un filtre de l'arbre : toutes les versions (le filtre les parcourt).
        void setFiltering(bool on) noexcept { filtering_ = on; }
        //  Epingles et Recents, en haut de la racine (avant API), chacun montre
        //  s'il n'est pas vide : des raccourcis vers d'autres noeuds, leur domaine
        //  en gris (hint). shortcutTarget : le noeud vise par un PinItem ou un
        //  RecentItem ; kInvalidNode sinon.
        struct ShortcutRow { ui::NodeId target{ui::kInvalidNode}; std::string hint; };
        void setShortcuts(std::vector<ShortcutRow> pins, std::vector<ShortcutRow> recents);
        [[nodiscard]] ui::NodeId shortcutTarget(ui::NodeId n) const;
        [[nodiscard]] static ui::NodeId pinsFolderNode() { return pack(NodeKind::PinsFolder, 0); }
        [[nodiscard]] static ui::NodeId recentsFolderNode() { return pack(NodeKind::RecentFolder, 0); }
        //  2e partie. LES COMPTEURS EN PASTILLE : le texte d'un dossier ne porte
        //  plus son nombre ("Types derives (28)", "Variables  [251]" -> le nom,
        //  et la pastille "28", "251" alignee a droite, le meme format partout).
        //  counterOf : le nombre d'un noeud ; vide : aucun.
        [[nodiscard]] std::string counterOf(ui::NodeId n) const;
        //  Ce qui a change depuis la derniere version : le point orange sur ces
        //  dossiers (leurs genres), son infobulle ("modifi\xC3\xA9 depuis V47").
        void setChangedKinds(std::vector<NodeKind> kinds, std::string tip);
        [[nodiscard]] bool changedSinceVersion(ui::NodeId n) const;
        //  Les mises a jour de bibliotheque : la pastille orange sur Types derives
        //  (ddt) et Blocs DFB (dfb), "N a mettre a jour" sur le titre API. Vrai : change.
        bool setLibraryUpdates(std::size_t ddt, std::size_t dfb);
        [[nodiscard]] std::size_t libraryUpdates() const noexcept { return libDdt_ + libDfb_; }
        //  La portee du rail (avec l'IHM ; sans elle : tout) : 0 tout, 1 Epingles
        //  (et Recents), 2 API, 3 IHM, 4 Simulation, 5 Versions - la racine ne
        //  montre que cela. Vrai : change.
        bool setScope(int scope);
        [[nodiscard]] int scope() const noexcept { return scope_; }
        //  Le domaine d'un noeud (sa couleur) : 1 API, 2 IHM, 3 Simulation,
        //  4 Versions ; 0 : aucun (la racine, Epingles, Recents).
        [[nodiscard]] std::uint8_t domainOf(ui::NodeId n) const;
        // ---- fin Lot API 8 : l'arbre du projet ----

    private:
        // ---- Lot API 8 : l'arbre du projet (2e partie) ----
        mutable bool          counterGuard_{false};   // text() avec son compteur (counterOf)
        std::vector<NodeKind> changedKinds_;
        std::string           changedTip_;
        std::size_t           libDdt_{0}, libDfb_{0};
        int                   scope_{0};
        mutable bool          decoGuard_{false};      // style() sans la decoration (decorate)
        void decorate(ui::NodeId n, ui::CellStyle& s) const;
        // ---- fin Lot API 8 ----
        // ---- Lot API 8 : l'arbre du projet ----
        [[nodiscard]] std::vector<domain::Index> hitsUnder(ui::NodeId) const;
        std::vector<FilterHitRow> filterHits_;
        mutable bool              hitGuard_{false};   // childCount / childAt sans les FilterHit
        std::size_t               hmiExprErrors_{0};
        std::size_t               hmiExprErrorsInViews_{0};
        mutable bool              styleGuard_{false}; // style() sans la pastille rouge
        bool                      allVersions_{false};
        bool                      filtering_{false};
        std::vector<ShortcutRow>  pins_, recents_;
        mutable bool              rootGuard_{false};  // childCount / childAt de la racine sans Epingles / Recents
        [[nodiscard]] std::size_t shortcutFolders() const noexcept { return (pins_.empty() ? 0u : 1u) + (recents_.empty() ? 0u : 1u); }
        [[nodiscard]] bool        briefVersions() const noexcept {
            return !allVersions_ && !filtering_ && versions_.size() > kBriefVersions + 1;
        }
        // ---- fin Lot API 8 : l'arbre du projet ----
        void rebuildLists();
        [[nodiscard]] const std::vector<domain::Index>* listFor(NodeKind) const noexcept;
        [[nodiscard]] const std::vector<domain::Index>* listById(domain::Index) const noexcept;
        // The interface folders a POU actually has; empty ones are not shown,
        // because an empty "Outputs" node is noise in a tree with 10 000 entries.
        [[nodiscard]] std::vector<NodeKind> folderKinds(domain::Index pouIndex) const;
        [[nodiscard]] std::vector<domain::Index> folderMembers(NodeKind, domain::Index pouIndex) const;
        // Lot API 7 : les cinq dossiers de portee seuls (Entrees, Sorties,
        // Entrees / sorties, Variables publiques, Variables privees), non vides.
        // Une unite de programme les montre comme un DFB, puis ses sections
        // DIRECTEMENT dessous (pas de dossier Sections : les sessions ecrivent
        // "API/Program units/Logigrammes_A/Matrice").
        [[nodiscard]] std::vector<NodeKind> scopeFolders(domain::Index pouIndex) const;

        // Lot API 7 : LES MEMBRES DEPLIES. Un membre est INTERNE : sa cle (la
        // variable racine et le chemin du membre) lui donne un rang fixe dans
        // `members_` - le NodeId d'un membre deplie reste le sien tant que le
        // modele vit, refresh() compris (seuls les enfants se recalculent).
        // Les enfants d'un noeud se calculent une fois (childCount, childAt et
        // hasChildren sont appeles bien des fois par image) ; refresh() les
        // oublie. Un rang hors de la table (un noeud d'un autre modele) : ni
        // enfant ni texte.
        struct MemberEntry {
            project::members::Node node;
            std::string            text;                          // ".sorties : Q   // commentaire"
            ui::Icon               icon{ui::Icon::Variable};
            ui::Tone               iconTone{ui::Tone::None};
            ui::Tone               fgTone{ui::Tone::None};
            domain::Index          root{domain::kNoIndex};        // la variable racine
            domain::Index          decl{domain::kNoIndex};        // la variable qui le declare (son commentaire)
            ui::NodeId             parent{ui::kInvalidNode};      // le noeud qui l'a deplie
        };
        // Le noeud de MemberTree derriere un noeud qui se deplie sur des membres,
        // sa variable racine et sa declaration ; faux : pas un tel noeud.
        [[nodiscard]] bool memberNodeOf(ui::NodeId, project::members::Node& node, domain::Index& root, domain::Index& decl) const;
        [[nodiscard]] const std::vector<ui::NodeId>& memberChildren(ui::NodeId) const;
        [[nodiscard]] bool memberHasChildren(ui::NodeId) const;
        [[nodiscard]] ui::NodeId internMember(ui::NodeId parentId, const project::members::Node& parent, project::members::Node child,
                                              domain::Index root, domain::Index parentDecl) const;
        // Un membre du type d'un de ses ancetres : un type qui se contient (un
        // projet abime) se deplierait sans fin, et "Tout deplier" avec lui.
        [[nodiscard]] bool memberRecurses(ui::NodeId) const;
        mutable std::vector<MemberEntry>                                members_;
        mutable std::unordered_map<std::string, domain::Index>          memberIds_;
        mutable std::unordered_map<ui::NodeId, std::vector<ui::NodeId>> memberKids_;
        mutable std::unordered_map<ui::NodeId, bool>                    memberHas_;

        ProjectRef project_;
        std::string simBadge_;                  // lot API 7
        ui::Tone    simTone_{ui::Tone::None};
        // ---- Lot API 8 : les pastilles du dossier Simulation (genre, texte, ton) ----
        struct SimBadge { NodeKind kind; std::string text; ui::Tone tone; };
        std::vector<SimBadge> simBadges_;

        // Les instantanes. Ils pointent dans project_->variables et
        // project_->pous ; `refresh()` les refait.
        std::vector<domain::Index> elementary_;
        std::vector<domain::Index> ddtInstances_;
        std::vector<domain::Index> dfbInstances_;
        std::vector<domain::Index> subroutines_;     // -> pous
        std::vector<std::string>   macros_;
        std::vector<std::string>   macroFolderOfMacro_;   // lot macros 1 : le dossier de chaque macro
        std::vector<std::string>   macroFolders_;         // lot macros 1 : les dossiers
        [[nodiscard]] std::vector<ui::NodeId> macroChildren(const std::string& folder) const;
        std::shared_ptr<const hmi::Document> hmi_;
        // 1.10 (chantier O) : les alarmes des objets, gardees par (vue, objet) et
        // oubliees quand le document change.
        ObjectAlarms objectAlarms_;
        mutable std::map<std::pair<std::uint64_t, std::uint64_t>, std::shared_ptr<const alarmtree::Group>> alarmCache_;
        // Les variables employees par l'IHM : un parcours de tout le projet, fait
        // une fois et refait quand le document change (pas a chaque dessin).
        struct UsedVariable { std::string path; bool hmi{false}; int uses{0}; };
        [[nodiscard]] const std::vector<UsedVariable>& usedVariables() const;
        mutable std::vector<UsedVariable> used_;
        std::vector<VersionRow>           versions_;
        mutable bool                      usedDirty_{true};
        core::ConnectionScope             hmiLinks_;
        // Lot API 4 : les nombres de Configuration (adresses du code, ports,
        // variables situees en %MW), comptes une fois par modele : le compte
        // parcourt tout le code, pas a chaque dessin.
        struct ConfigCounts { std::size_t channels{0}, faulty{0}, network{0}, memory{0}; };
        [[nodiscard]] const ConfigCounts& configCounts() const;
        mutable std::optional<ConfigCounts> configCounts_;
        std::shared_ptr<const hmi::build::Cache> buildState_;   // 1.11 (chantier T3, C4)
    };

    // --------------------------------------------- library explorer (TreeView) ---
    //   Libraries
    //     ?? Standard
    //     ?? Motion
    //     ?? Process
    //     ?? Custom ? <DFB type> (instances: n)
    class LibraryTreeModel final : public ui::ITreeModel {
    public:
        explicit LibraryTreeModel(ProjectRef project);
        [[nodiscard]] ui::NodeId  root() const override;
        [[nodiscard]] std::size_t childCount(ui::NodeId) const override;
        [[nodiscard]] ui::NodeId  childAt(ui::NodeId, std::size_t) const override;
        [[nodiscard]] bool          hasChildren(ui::NodeId) const override;
        [[nodiscard]] std::string   text(ui::NodeId) const override;
        [[nodiscard]] ui::CellStyle style(ui::NodeId) const override;
    private:
        ProjectRef                         project_;
        std::vector<domain::LibraryKind>   families_;
        std::vector<std::vector<domain::Index>> byFamily_;
    };

    // ----------------------------------------- PLC configuration (PropertyGrid) ---
    // Builds the Name/Value categories shown in the centre pane. Returns the
    // "hardware is inferred" notice as its own category rather than silently
    // showing a rack that was never in the file.
    std::vector<ui::PropertyGrid::Category> buildConfigurationProperties(const domain::Project&);

    // The two summary panes along the bottom of the workspace.
    // Everything the export says about one module, including the channel table.
    std::vector<ui::PropertyGrid::Category> buildModuleProperties(const domain::Project&,
        const domain::Module&);
    std::vector<ui::PropertyGrid::Category> buildAnalysisSummary(const importer::AnalysisReport&);
    std::vector<ui::PropertyGrid::Category> buildProjectStatus(const domain::Project&,
        const importer::AnalysisReport&);

    // ------------------------------------------- sections (TableView) -------------
    class SectionTableModel final : public ui::ITableModel {
    public:
        enum Column : std::size_t { Name, Task, Language, Lines, Activation, ColumnCount };
        explicit SectionTableModel(ProjectRef project);

        [[nodiscard]] std::size_t rowCount() const override;
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] bool less(ui::RowIndex, ui::RowIndex, std::size_t) const override;
    private:
        ProjectRef project_;
    };

    // ------------------------------------------- diagnostics (TableView) ----------
    class DiagnosticsTableModel final : public ui::ITableModel {
    public:
        enum Column : std::size_t { Severity, Kind, Subject, Detail, ColumnCount };
        DiagnosticsTableModel(ProjectRef project, importer::AnalysisReport report);

        [[nodiscard]] std::size_t rowCount() const override { return report_.findings.size(); }
        [[nodiscard]] std::size_t columnCount() const override { return ColumnCount; }
        [[nodiscard]] std::string headerText(std::size_t) const override;
        [[nodiscard]] std::string cellText(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] ui::CellStyle cellStyle(ui::RowIndex, std::size_t) const override;
        [[nodiscard]] bool less(ui::RowIndex, ui::RowIndex, std::size_t) const override;

        [[nodiscard]] const importer::AnalysisReport& report() const noexcept { return report_; }
    private:
        ProjectRef              project_;
        importer::AnalysisReport report_;
    };

} // namespace app