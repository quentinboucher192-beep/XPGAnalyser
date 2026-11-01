// =============================================================================
//  app/ai/AiFileModels.hpp - 1.21.0 : LES MODELES DE FICHIERS (les << patterns >>)
// -----------------------------------------------------------------------------
//  Un classeur deja vu se relit sans analyse : son MODELE est garde dans la
//  memoire de l'IA. Un modele =
//
//    - son NOM ("Liste E/S armoire 3") et son GENRE (E/S, reseau, reports) ;
//    - sa SIGNATURE : la feuille, la ligne d'en-tete et ses intitules
//      normalises (AiWorkbook.hpp : headerCells) - un classeur qui porte ces
//      intitules (sur cette ligne, ou plus bas : un titre ajoute) est reconnu,
//      meme avec d'autres lignes ;
//    - la CORRESPONDANCE role -> colonne et les REGLES de lecture (les lignes
//      sautees sous l'en-tete, les valeurs vides, les fusions, les colonnes
//      recopiees vers le bas, les roles cles) ;
//    - sa date, son nombre d'utilisations.
//
//  OU IL VIT : une connaissance AJOUTEE du registre (AiKnowledge.hpp :
//  connaissances/<id>.json, le meme contrat que les autres), de categorie
//  << modele-fichier >>, source << fichier >>, portee << technique >> ; le modele
//  structure est dans son champ "donnees", et son "contenu" est la description
//  lisible - ce que N'IMPORTE QUEL fournisseur recoit avec ses demandes (les
//  connaissances actives, AiPrompt.cpp), Anthropic comme un service compatible
//  OpenAI ; l'IA integree s'en sert directement. Il se voit, se renomme, se
//  desactive et se supprime dans << Memoire et apprentissage >> (le theme
//  << Modeles de fichiers >>) ; l'export .xia l'emporte vers un autre PC
//  (AiLearningFile.hpp). Le modele distant n'est jamais reentraine : le modele
//  de fichier est une donnee locale, relue et envoyee.
//
//  A L'IMPORT (le fil d'analyse : recognize) : le classeur est compare aux
//  modeles connus ; un modele reconnu le lit tout de suite (<< Modele reconnu :
//  <nom> - 412 lignes lues >>) ; sinon la detection propose un genre et une
//  correspondance. Le choix fait dans la fenetre << Modele... >> est garde avec
//  la piece jointe (choiceOf / setChoice).
//
//  CE QUE L'IA EN FAIT (plan*) : TOUJOURS UNE PROPOSITION (l'apercu, puis
//  Appliquer) - creer les variables IHM (le nom depuis le repere, le type depuis
//  DI/DO/AI/AO, BOOL/INT/REAL ou l'adresse, la description depuis la
//  designation), un type IHM par famille d'equipements repetee (P101_MARCHE,
//  P101_DEFAUT, P102_MARCHE... -> T_P avec MARCHE et DEFAUT, une variable par
//  equipement : l'equivalent IHM des DDT), les equipements d'un fichier reseau,
//  les adresses %MW / %M d'un equipement Modbus. Jamais un nom, une adresse, une
//  ligne inventes : les lignes sans repere, illisibles, en double sont dites.
//  L'IA connectee lit les lignes par pages (l'outil kRowsTool) plutot que le
//  texte brut du classeur.
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "AiStore.hpp"
#include "AiWorkbook.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::memory { class Registry; }

namespace app::ai::sheets {

inline constexpr std::string_view kModelCategory = "modele-fichier";
inline constexpr int              kModelFormat = 1;   // le format du champ "donnees"

struct FileModel {
    std::string              id, name;
    Layout                   layout;        // genre, feuille, ligne d'en-tete, colonnes, regles
    std::vector<std::string> signature;     // les intitules normalises de la ligne d'en-tete, par colonne
    int                      uses{0};
    std::string              added, modified, lastUsed;   // ISO
    bool                     active{true};
    std::string              source{"fichier"};
    std::string              file;          // connaissances/<id>.json
};

// ---- la memoire (le registre des connaissances) ---------------------------------------------------------------
[[nodiscard]] std::vector<FileModel> modelsIn(const memory::Registry& reg, bool withInactive = false);
[[nodiscard]] std::vector<FileModel> loadModels(bool withInactive = false);   // relit le registre (n'importe quel fil)
[[nodiscard]] std::optional<FileModel> findModel(std::string_view idOrName);
// Un modele tire d'un classeur et d'une mise en page (la signature : les intitules de la ligne d'en-tete).
[[nodiscard]] FileModel makeModel(const Workbook& wb, const Layout& layout, std::string name);
// ENREGISTRER : neuf (id vide : il est attribue, stable - fichier.<nom>.<empreinte de la signature>) ou mis a jour. Refuse :
// un nom vide, un nom deja porte par un autre modele, aucune colonne, aucune signature.
bool saveModel(FileModel& m, std::string* why = nullptr);
bool renameModel(std::string_view id, const std::string& name, std::string* why = nullptr);
bool removeModel(std::string_view id, std::string* why = nullptr);
// Une utilisation de plus (un classeur reconnu, un modele choisi) - sur le fil de l'interface.
bool noteUse(std::string_view id);
// Le texte lisible d'un modele (le "contenu" de sa connaissance : ce que les fournisseurs recoivent).
[[nodiscard]] std::string describe(const FileModel& m);
// "repere = A (Rep\xC3\xA8re), designation = B..." : la correspondance en une ligne.
[[nodiscard]] std::string mappingText(const Layout& L);

// ---- reconnaitre ------------------------------------------------------------------------------------------------
struct Match {
    std::size_t model{0}, sheet{0}, headerRow{0};
    double      score{0};
};
// Le modele qui reconnait une feuille du classeur (ses intitules a l'identique, a 90 % au moins et tous ceux des roles).
[[nodiscard]] std::optional<Match> match(const Workbook& wb, const std::vector<FileModel>& models);

struct Recognition {
    Genre                    genre{Genre::Other};
    double                   confidence{0};
    bool                     hasLayout{false};   // un modele reconnu, ou une detection d'un genre (pas << autre >>)
    Layout                   layout;
    std::string              modelId, modelName;  // reconnu ; vide : la detection
    std::size_t              rows{0}, ignored{0}, empty{0};
    Detection                detection;
    std::vector<std::pair<std::string, std::size_t>> sheetsRead;   // 1.21.1 : les feuilles lues et leurs lignes (la premiere : celle de la mise en page)
    std::string              summary;            // "Mod\xC3\xA8le reconnu : <nom> \xE2\x80\x94 412 lignes lues"
    std::vector<std::string> details;
};
[[nodiscard]] Recognition recognize(const Workbook& wb, const std::vector<FileModel>& models);

// ---- le JSON (l'analyse d'une piece : extra "classeur" ; le choix : extra "modeleFichier") -----------------------
[[nodiscard]] json::Value layoutToJson(const Layout& L);
[[nodiscard]] Layout      layoutFromJson(const json::Value& v);
[[nodiscard]] json::Value recognitionToJson(const Recognition& r);
// L'analyse d'extraction d'un classeur recoit sa reconnaissance (extra "classeur", le resume, les details).
void applyRecognition(AttachmentAnalysis& an, const Recognition& r);

// ---- le choix garde avec la piece jointe ----------------------------------------------------------------------
struct Choice {
    Layout      layout;
    std::string modelId, modelName;
    bool        byUser{false};
    double      confidence{0};
    std::size_t rows{0};   // les lignes lues avec cette mise en page (0 : inconnu)
};
// Le choix de l'utilisateur (extra "modeleFichier"), sinon la reconnaissance de sa derniere analyse - si elle a donne
// un modele ou un genre ; nullopt : rien de structure.
[[nodiscard]] std::optional<Choice> choiceOf(const Attachment& a);
// La reconnaissance de sa derniere analyse (meme << autre >>) ; nullopt : pas analysee, pas un classeur.
[[nodiscard]] std::optional<Recognition> recognitionOf(const Attachment& a);
void setChoice(Attachment& a, const Choice& c);
void clearChoice(Attachment& a);
// Le classeur joint, pour l'IA (le classeur lu : `workbook`, nul s'il ne l'est pas encore).
[[nodiscard]] Attached attachedOf(const Attachment& a, std::string path, std::shared_ptr<const Workbook> workbook);

// ---- lire ce qu'il faut -----------------------------------------------------------------------------------------
// La feuille de la mise en page, les feuilles JUMELLES (la meme ligne d'en-tete, aux memes intitules : << Lectures >>
// et << Ecritures >> d'une table d'echange) et, 1.21.1, TOUTES LES AUTRES FEUILLES DU MEME GENRE, chacune avec sa propre
// detection (une liste d'E/S en plusieurs feuilles : entrees TOR, sorties TOR, analogiques..., aux en-tetes differents) -
// chacune lue. Une feuille masquee n'est pas lue. `detection` : celle du classeur, si elle est deja faite.
[[nodiscard]] std::vector<Reading> readAll(const Workbook& wb, const Layout& layout, const Detection* detection = nullptr);

// ---- ce que l'IA en fait : TOUJOURS une proposition ------------------------------------------------------------
struct PlanOptions {
    bool        types{false};          // une famille d'equipements repetee -> un type IHM et une variable par equipement
    std::string equipment;             // l'equipement des adresses memoire (vide : le seul Modbus du projet)
    bool        address{true};         // poser les adresses %MW / %M (adresser) quand l'equipement est connu
    bool        onlyExisting{false};   // adresser les variables du fichier qui existent deja (rien a creer)
    std::size_t maxRows{2000};
};
struct Plan {
    std::vector<Operation> ops;
    std::string            summary;   // le libelle de la proposition
    std::string            text;      // la reponse (markdown simple, au tutoiement)
    bool                   ok{false}; // des operations (sinon `text` dit pourquoi)
};
[[nodiscard]] Plan planVariables(const Attached& f, const std::vector<Reading>& readings, const hmi::Project* project, const PlanOptions& o = {});
[[nodiscard]] Plan planEquipments(const Attached& f, const std::vector<Reading>& readings, const hmi::Project* project);
// Ce que contient le classeur (une question) : le genre, la mise en page, quelques lignes, ce que l'IA peut en faire.
[[nodiscard]] std::string describeFile(const Attached& f, const std::vector<Reading>& readings);
// Un nom de repere -> un identifiant (les caracteres interdits deviennent _, un chiffre en tete prend un _) ; vide : rien
// d'utilisable. `adjusted` : le nom a change.
[[nodiscard]] std::string identifierFrom(std::string_view raw, bool* adjusted = nullptr);

// ---- l'IA connectee -----------------------------------------------------------------------------------------------
inline constexpr std::string_view kRowsTool = "lignes_classeur";
inline constexpr std::size_t      kRowsPage = 200;   // au plus, par appel
[[nodiscard]] std::string rowsToolJson();
// La reponse a un appel (son entree JSON) : les lignes lues, par pages ; sur le fil du fournisseur (un classeur pas encore
// lu l'est ici, depuis sa copie).
[[nodiscard]] std::string runRowsTool(const std::vector<Attached>& files, std::string_view inputJson, bool* isError = nullptr);
// Les classeurs joints, pour les consignes (vide : aucun classeur structure). `tool` : l'outil des lignes est offert.
[[nodiscard]] std::string workbooksPrompt(const std::vector<Attached>& files, bool tool);
// Ce qui part a la place du texte brut d'un classeur structure (l'outil offert) : sa lecture, ses premieres lignes.
[[nodiscard]] std::string compactBlock(const Attached& f, std::size_t sampleRows = 15);

} // namespace app::ai::sheets
