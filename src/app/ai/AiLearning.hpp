// =============================================================================
//  app/ai/AiLearning.hpp - 1.17.0 : ce que l'IA integree sait, MODIFIABLE
// -----------------------------------------------------------------------------
//  "APPRENDRE" ICI = la memoire locale (le modele distant n'est jamais
//  reentraine). Deux sortes de savoir STRUCTURE, que l'IA integree (AiLocal)
//  LIT A CHAQUE DEMANDE - une correction faite dans l'onglet Apprentissage
//  change sa reponse suivante :
//
//    - LES MODELES METIER (T_Capteur, T_Pompe...) : ceux du logiciel
//      (builtInModels, la table d'AiLocal) et ceux de l'utilisateur. Un modele
//      integre se CORRIGE (sa correction est gardee a part ; revertModel la
//      retire), se DESACTIVE ; un modele de l'utilisateur s'ajoute, se change,
//      se supprime ;
//    - LE VOCABULAIRE : un mot = un genre d'objet (<< bache >> -> Cuve), un mot
//      = un modele metier (<< compresseur >> -> T_Compresseur), un mot a ignorer
//      (un mot-outil). Les mots integres (les synonymes du logiciel) se lisent ;
//      ceux de l'utilisateur s'ajoutent, se desactivent, s'oublient.
//
//  LE STOCKAGE : des connaissances AJOUTEES comme les autres (AiKnowledge.hpp :
//  connaissances/<id>.json, le meme contrat), de categorie "modeles" ou
//  "vocabulaire", avec leur savoir structure dans le champ "donnees" et un
//  "contenu" lisible (ce que les IA connectees recoivent). L'export .xia
//  (AiLearningFile.hpp) les emporte sans rien de plus. Les identifiants sont
//  stables : "modele.<cle>", "mot.<mot replie>".
//
//  Les mots se comparent REPLIES (fold : minuscules, sans accents).
// =============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::learning {

struct ModelMember {
    std::string name, type, description;
    bool operator==(const ModelMember&) const = default;
};

struct BusinessModel {
    std::string              key;            // "pompe", "compresseur" (minuscules, sans accent)
    std::string              name;           // le type propose : T_Pompe
    std::string              label;          // "pompe" (dans la reponse)
    std::string              description;
    std::vector<std::string> words;          // les mots qui l'appellent (replies) : pompe, pompes...
    std::vector<ModelMember> members;
    bool                     builtIn{false};     // du logiciel
    bool                     corrected{false};   // un modele du logiciel, corrige par l'utilisateur
    bool                     active{true};
    std::string              source{"logiciel"}; // "logiciel", "utilisateur", "assistant", "fichier"
    std::string              added, modified;    // ISO (une correction, un ajout) ; vide : du logiciel
};

enum class Meaning : unsigned char { Kind, Model, Ignore };

struct WordEntry {
    std::string word;            // tel qu'ecrit (accents compris) ; compare replie
    Meaning     means{Meaning::Kind};
    std::string target;          // Kind : la cle du genre (hmi::kindKey, "Tank") ; Model : la cle du modele ; Ignore : vide
    bool        builtIn{false};
    bool        active{true};
    std::string source{"logiciel"};
    std::string added, modified;
};

// ---- les tables du logiciel (AiLocal.cpp les donne : elles sont les siennes) ----
[[nodiscard]] const std::vector<BusinessModel>& builtInModels();
[[nodiscard]] const std::vector<WordEntry>&     builtInWords();

// ---- ce qui vaut maintenant (le logiciel, corrige, plus l'utilisateur) - relu du disque quand il change ----
[[nodiscard]] std::vector<BusinessModel> models(bool withInactive = false);
[[nodiscard]] std::optional<BusinessModel> model(std::string_view key);        // actif seulement
[[nodiscard]] std::vector<WordEntry> words(bool withInactive = false);
[[nodiscard]] std::string modelForWord(std::string_view folded);               // la cle du modele d'un mot ; "" : aucun
[[nodiscard]] bool        ignoredWord(std::string_view folded);                // un mot appris a ignorer
[[nodiscard]] std::vector<WordEntry> learnedKindWords();                       // les mots appris qui designent un genre (actifs)

// ---- corriger, apprendre (chaque geste : son fichier, sa ligne d'activite) ----
// Un modele du logiciel : sa correction ; un autre : l'ajouter ou le changer. Refuse : une cle vide, un nom qui n'est pas
// un identifiant, un membre sans nom ou d'un type que le logiciel ne connait pas (la raison dans `why`).
bool saveModel(const BusinessModel& m, std::string* why = nullptr, std::string_view source = "utilisateur");
bool revertModel(std::string_view key, std::string* why = nullptr);            // retirer la correction d'un modele du logiciel
bool setModelActive(std::string_view key, bool on, std::string* why = nullptr);
bool removeModel(std::string_view key, std::string* why = nullptr);            // un modele de l'utilisateur
bool learnWord(const WordEntry& w, std::string* why = nullptr, std::string_view source = "utilisateur");
bool setWordActive(std::string_view word, bool on, std::string* why = nullptr);
bool forgetWord(std::string_view word, std::string* why = nullptr);

// Le texte lisible d'un modele, d'un mot (le "contenu" de sa connaissance).
[[nodiscard]] std::string describe(const BusinessModel& m);
[[nodiscard]] std::string describe(const WordEntry& w);
// Un type de membre valide ? (les types elementaires du logiciel, un tableau d'eux, un type IHM ecrit) - sinon la raison.
[[nodiscard]] bool validMemberType(std::string_view type, std::string* why = nullptr);

void invalidate();   // apres une ecriture faite ailleurs (un import) : relire au prochain acces

// ---- LES MESURES (apprentissage.jsonl, a cote d'activite.jsonl) ---------------------------------------------------
//  Une ligne par reponse de l'IA INTEGREE (son fournisseur, ou << IA integree d'abord >>) : la date, comprise ou non, la
//  demande (200 caracteres au plus), une proposition ou non. A part d'activite.jsonl : une ligne par demande noierait le
//  journal d'activite (son onglet). Ce que deviennent les propositions se lit dans les conversations (leur etat).
struct Measure {
    std::string date, request;
    bool        understood{false}, proposal{false};
};
[[nodiscard]] std::string measuresFile();
void recordReply(bool understood, std::string_view request, bool proposal);
[[nodiscard]] std::vector<Measure> readMeasures();

// Une proposition (de n'importe quelle IA) et ce qu'elle est devenue : sa date, gardee (appliquee) ou non (refusee) ;
// une proposition encore a decider ne compte pas.
struct Decision {
    std::string date;
    bool        kept{false};
};

// Une semaine (du lundi 0 h au lundi suivant, heure du PC).
struct Week {
    long long start{0};                      // time_t du lundi
    int       asked{0}, understood{0};       // les demandes a l'IA integree, comprises
    int       decided{0}, kept{0};           // les propositions decidees, gardees
    int       byUser{0}, byAssistant{0}, byFile{0};   // les elements appris (ajoutes ou corriges), par origine
};
// Les `count` dernieres semaines (la plus ancienne d'abord), jusqu'a celle de `nowT` comprise.
[[nodiscard]] std::vector<Week> weeks(int count, long long nowT, const std::vector<Measure>& measures, const std::vector<Decision>& decisions);
// Les demandes non comprises des `days` derniers jours (les plus recentes d'abord, sans doublon), sauf celles qu'on a ecartees.
[[nodiscard]] std::vector<Measure> notUnderstood(int days, long long nowT);
void dismissRequest(std::string_view request);   // << Ignorer >> : la demande quitte << A lui apprendre >>
// Les propositions decidees des conversations du dossier IA (appliquees : gardees ; refusees : non).
[[nodiscard]] std::vector<Decision> decisions();

// ---- << ESSAYER UNE PHRASE >> : l'IA integree la lit sur le projet ouvert, SANS RIEN APPLIQUER ----
//  L'ecran du projet pose le testeur (il connait le projet et la vue) ; sans lui, rien a essayer.
struct Trial {
    bool        understood{false};
    std::string summary;    // << le type T_Capteur : Mesure (REAL), ... >> ; pas comprise : la reponse
};
using PhraseTester = std::function<Trial(const std::string&)>;
void setPhraseTester(PhraseTester tester);
[[nodiscard]] bool hasPhraseTester();
[[nodiscard]] Trial tryPhrase(const std::string& phrase);

} // namespace app::ai::learning
