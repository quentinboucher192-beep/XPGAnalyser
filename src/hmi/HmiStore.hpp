// =============================================================================
//  hmi/HmiStore.hpp - le projet IHM sur disque, dans le dossier du projet
// -----------------------------------------------------------------------------
//  COMME LE RESTE DU PROJET : DU TEXTE, UN FICHIER PAR VUE.
//
//      MonProjet/
//        ihm/ihm.txt                  configuration, vues, ressources, fichiers externes
//        ihm/vues/0003-Synoptique.vue une vue : calques, objets, scripts
//        ihm/ressources/0012-logo.png le fichier d'une ressource, tel qu'importe
//        ihm/corbeille/               ce que l'index ne designe plus (vue, ressource)
//
//  Un diff se lit, deux personnes travaillent sur deux vues sans conflit, et un
//  fichier se relit a la main quand il le faut.
//
//  AUCUNE PERTE DE DONNEES : chaque fichier s'ecrit a cote (".tmp") puis prend
//  la place de l'ancien d'un seul renommage ; l'index s'ecrit en dernier, donc
//  il ne designe jamais une vue qui n'est pas encore sur le disque. Chaque
//  fichier se termine par "fin" : un fichier coupe (disque plein, copie
//  interrompue) est refuse au chargement au lieu d'etre lu a moitie. Une vue
//  qui disparait de l'index n'est pas effacee : son fichier part a la corbeille.
//
//  Le format ligne par ligne : un mot, puis des cles=valeurs ; une valeur avec
//  des espaces ou des guillemets est entre guillemets, avec \" \\ \n \t.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hmi {

struct LoadReport {
    std::vector<std::string> warnings;    // cles inconnues, references cassees...
};

// 2 : ressources (ihm/ressources/) et fichiers externes ; 3 : programmation
// generale (ihm/scripts/, variables IHM), actions, cycle IHM. Un outil plus ancien
// refuse un format plus recent au lieu de le relire en perdant ce qu'il ne
// connait pas - et de l'effacer au premier enregistrement.
// 4 : alarmes, recettes, utilisateurs et securite, reglages des historiques.
inline constexpr int kFormatVersion = 23;   // 5 : lot 6 (roles des vues, heritage, en-tete, pied) ; 6 : lot 7 (fonctions IHM) ;
                                           // 7 : lot 8 (popups, parametres de vue, champ de saisie, objets utilisateurs) ;
                                           // 8 : lot 9 (23 commandes et afficheurs, confirmation des boutons) - un
                                           //     outil du lot 8 les lirait comme des rectangles : il refuse ;
                                           // 9 : lot 10 (l'objet Parametres systeme, les symboles de synoptique,
                                           //     les symboles reutilisables) - un outil du lot 9 refuse ;
                                           // 10 : lot 11 (graphiques, objets des alarmes et de la production,
                                           //     consigne des alarmes, sons par priorite) - un outil du lot 10 refuse ;
                                           // 11 : lot 12 (objets de navigation et de structure, vue parente, zoom,
                                           //     vue de demarrage par groupe, styles nommes) - un outil du lot 11 refuse
                                           // 12 : lot 13 (politique des mots de passe, verrouillage, badge, journal
                                           //     d'audit, signature des commandes, langues, unites, accessibilite)
                                           //     - un outil du lot 12 refuse
                                           // 13 : lot 14 (communication Modbus TCP, objets de la communication,
                                           //     poste d'exploitation, notifications, rapports, acces web)
                                           //     - un outil du lot 13 refuse
                                           // 14 : lot 15 (les equipements du reseau, les variables IHM liees a un
                                           //     equipement, leur mise a l'echelle) - un outil du lot 14 refuse
                                           // 15 : lot 16 (les types IHM, les tableaux, les dossiers des variables,
                                           //     les adresses des membres, l'objet GIF anime) - un outil du lot 15 refuse
                                           // 16 : lot 17 (les zones memoire, les jumeaux simules et leurs
                                           //     comportements, les ports simules) - un outil du lot 16 refuse
                                           // 17 : lot 18 (les valeurs simulees : un comportement coche ou non,
                                           //     les cases forcees d'un jumeau) - un outil du lot 17 refuse
                                           // 18 : lot 20 (les modeles de vues gardes dans le projet :
                                           //     ihm/modeles/) - un outil du lot 19 refuse
                                           // 19 : lot 21 (les dossiers des vues, popups, modeles, symboles,
                                           //     scripts, types, styles et ressources) - un outil du lot 20 refuse
                                           // 20 : 1.9 (l'esclave simule lie : bascule automatique, lecture
                                           //     sur le poste ; la page Simulation du poste ; les jeux de
                                           //     lecture de l'outil Modbus) - une 1.8 refuse
                                           // 21 : 1.10 (les operateurs des symboles et des types IHM ; les
                                           //     enumerations IHM : type_genre, valeur_enum) - une 1.9
                                           //     refuse
                                           // 22 : 1.10.2 (les groupes d'alarmes et leurs reglages :
                                           //     groupe_alarmes ; les liens des groupes des objets :
                                           //     lien_groupe_alarmes) - une 1.10.1 refuse ; un projet 21
                                           //     s'ouvre tel quel
                                           // 23 : 1.11.18 (refonte des scripts, lot 3 : les declarations des
                                           //     scripts, fonctions, redefinitions et operateurs dans le
                                           //     modele : declaration) - une 1.11.17 refuse ; un projet sans
                                           //     declaration du modele s'ecrit encore au format 22 (ou 21)

// "2026-09-21 14:05" : l'horodatage des dates de creation et de modification.
[[nodiscard]] std::string   nowStamp();

[[nodiscard]] bool          exists(const std::string& projectFolder);
[[nodiscard]] core::Status  save(const Project&, const std::string& projectFolder);
[[nodiscard]] core::Result<Project> load(const std::string& projectFolder,
                                         LoadReport* report = nullptr);

// LE PROJET EN FICHIERS, SANS DISQUE : ce que save() ecrit (chemins relatifs a
// ihm/ : "ihm.txt" en tete, "vues/...", "scripts/...", "ressources/..."), et
// ce que l'archive d'export contient. parseProject relit par une fonction de
// lecture : le dossier (load) ou les entrees d'une archive (import).
struct ProjectFile {
    std::string                  path;
    std::shared_ptr<const Bytes> data;
};
[[nodiscard]] std::vector<ProjectFile> serializeProject(const Project&);
using FileReader = std::function<bool(const std::string& relativePath, std::string& content)>;
[[nodiscard]] core::Result<Project>    parseProject(const FileReader&, LoadReport* report = nullptr);

// Les memes, en memoire : l'export et les tests s'en servent.
[[nodiscard]] std::string            serializeView(const View&);
[[nodiscard]] core::Result<View>     parseView(std::string_view text, LoadReport* report = nullptr);
[[nodiscard]] std::string            viewFileName(const View&);

// Une ligne "mot cle=valeur ...", decoupee. Expose pour les tests.
struct Record {
    std::string word;
    std::vector<std::pair<std::string, std::string>> fields;
    [[nodiscard]] const std::string* get(std::string_view key) const noexcept;
};
[[nodiscard]] bool        parseRecord(std::string_view line, Record& out, std::string& error);
[[nodiscard]] std::string quote(std::string_view);

} // namespace hmi
