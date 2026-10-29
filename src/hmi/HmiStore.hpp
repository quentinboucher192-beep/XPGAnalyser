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
inline constexpr int kFormatVersion = 33;   // 5 : lot 6 (roles des vues, heritage, en-tete, pied) ; 6 : lot 7 (fonctions IHM) ;
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
                                           // 24 : 1.11.23 (les raccourcis des vues : les declencheurs Touche
                                           //     enfoncee, relachee, maintenue, repetee et la touche=) - une
                                           //     1.11.22 refuse (elle perdrait ces actions) ; un projet sans
                                           //     raccourci s'ecrit encore au format 23 (ou 22, 21)
                                           // 25 : 1.12.13 (les gestes sur un tableau : Clic sur une case, Case
                                           //     saisie, Ligne choisie... et colonne=) - une 1.12.12 refuse (elle
                                           //     perdrait ces actions) ; un projet sans eux s'ecrit encore au
                                           //     format 24 (ou 23, 22, 21)
                                           // 26 : 1.12.16 (les constantes et les fonctions des vues et des popups,
                                           //     constantes_vue, portee=privee, ouverture=oui) - une 1.12.15 refuse
                                           //     (elle les perdrait) ; un projet sans elles s'ecrit au format 25
                                           // 27 : 1.12.20 (les types rappel nommes : type_genre genre=rappel
                                           //     signature=...) - une 1.12.19 refuse (elle en ferait des structures
                                           //     vides) ; un projet sans eux s'ecrit au format 26 (ou 25...)
                                           // 28 : 1.12.20, lot G (les generiques : generique nom=T contrainte=...
                                           //     apres un type IHM, un symbole, une popup) - une 1.12.19 refuse ;
                                           //     un projet sans eux s'ecrit au format 27 (ou 26...)
                                           // 29 : 1.13.0, lot J4 (les tests unitaires des fonctions : cas_test / arg /
                                           //     pose / verif apres une fonction, comme les lignes declaration) - une
                                           //     1.12.31 refuse (elle perdrait les cas) ; un projet dont aucune fonction
                                           //     n'a de cas s'ecrit au format 28 (ou 27...)
                                           // 30 : 1.13.0 (les generiques d'une fonction : generique nom=T
                                           //     contrainte=... apres une fonction ou une fonction_symbole) -
                                           //     une 1.12.x refuse ; un projet dont aucune fonction n'est
                                           //     generique s'ecrit encore au format 28 (ou moins)
                                           // 31 : 1.13.0, lot J1 (les objets : les methodes d'un type -
                                           //     methode_type ; l'heritage - type_ihm herite=... ; les
                                           //     interfaces - type_genre genre=interface et type_ihm
                                           //     implemente=... ; la visibilite d'un membre - membre
                                           //     portee=privee) - une IHM Studio d'avant refuse (elle
                                           //     perdrait methodes, heritage et interfaces) ; un type sans
                                           //     methode, sans base, sans interface et sans membre prive
                                           //     s'ecrit au format d'avant (30 ou moins)
                                           // 32 : 1.13.0, lot J6 (les unites de mesure : unite= sur une ligne
                                           //     membre / variable / declaration / fonction ; la bibliotheque
                                           //     des unites du projet - famille_unite / unite_mesure ; l'option
                                           //     unites_strictes) - une IHM Studio d'avant refuse (elle perdrait
                                           //     les unites et leurs controles) ; un projet sans aucune unite,
                                           //     sans bibliotheque d'unites et sans l'option s'ecrit au format
                                           //     d'avant (31 objets, 30 generiques de fonction... en escalier)
                                           // 33 : 1.13.0, lot J8 (les espaces de noms : espace= sur un type
                                           //     IHM, une fonction, un script ; les espaces vides gardes -
                                           //     dossier_liste liste=espaces ; les bibliotheques reliees -
                                           //     bibliotheque, bib_type, bib_fonction, bib_script) - une IHM
                                           //     Studio d'avant refuse (elle perdrait les espaces et les liens) ;
                                           //     un projet sans espace et sans bibliotheque s'ecrit au format
                                           //     d'avant (32 unites, 31 objets... en escalier)

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

// ---- 1.13.1 : LE CODE BRUT D'UN OBJET ----
//  "dans tous les objets des pages/vues avoir un onglet 'code brut' que l'on peut copier coller" :
//  le texte complet d'un objet - ses lignes du fichier de sa vue, telles que l'enregistrement les
//  ecrit (objet, prop, redefinition et ses declarations, surcharge_alarme, action objet=...), ses
//  enfants a la suite s'il en a (un groupe), sous un en-tete, jusqu'a "fin". Rien d'invente : il se
//  relit par le lecteur des vues (parseView), sans perte. Les ids n'y servent qu'a relier les lignes
//  (le parent d'un enfant, l'objet d'une action) : ce qui l'applique ou le colle en donne de neufs.
inline constexpr std::string_view kObjectCodeHeader = "# IHM Studio - code brut d'un objet";
// Le code des objets `ids` de la vue (un groupe : ses enfants avec lui ; un enfant dont le groupe est
// aussi demande : une seule fois). Vide : aucun de ces objets.
[[nodiscard]] std::string serializeObjectCode(const View&, const std::vector<Id>& ids);
// Ce texte est-il un code brut d'objet (son en-tete, ou une premiere ligne "objet ...") ?
[[nodiscard]] bool isObjectCode(std::string_view text);
struct ObjectCode {
    std::vector<Object> objects;   // dans l'ordre du code (l'ordre de dessin d'origine)
    std::vector<Id>     roots;     // ceux du haut : ni enfant d'un autre objet du code
};
// Relire un code brut. Une faute fait echouer, rien n'est rendu a moitie : la ligne et ce qui ne va
// pas ("ligne 3 : 'cle' sans '='", "ligne 7 : genre d'objet inconnu 'boutton'", "la ligne 'fin'
// manque : le code est coupe ?").
[[nodiscard]] core::Result<ObjectCode> parseObjectCode(std::string_view text);

// Une ligne "mot cle=valeur ...", decoupee. Expose pour les tests.
struct Record {
    std::string word;
    std::vector<std::pair<std::string, std::string>> fields;
    [[nodiscard]] const std::string* get(std::string_view key) const noexcept;
};
[[nodiscard]] bool        parseRecord(std::string_view line, Record& out, std::string& error);
[[nodiscard]] std::string quote(std::string_view);

} // namespace hmi
