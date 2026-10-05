#pragma once
// Le point d'entree des tutoriels (1.11, T1) : ce que le centre d'aide (T2)
// appelle pour en lancer un, sans rien savoir de l'ecran.
//
//     help::startTutorial("objet-vanne", "r\xC3\xA9glante");          // par l'id du tutoriel
//     help::startTutorial("objet-vanne", {}, {.step = 3, .paused = true});  // "Me montrer"
//     help::startTutorial("objet-vanne", {}, {.aTry = true});         // "Essayer"
//
// La cle est l'id d'un tutoriel (= <id> | ...) ou un sujet du centre d'aide
// (@sujet). Les tutoriels connus sont ceux qu'on a inscrits (registerTutorialText :
// le texte embarque par le generateur en tranche 3, ou un fichier .tuto lu) ;
// l'appli inscrit le LANCEUR (setTutorialLauncher) : il ouvre le bac a sable, le
// calque et le lecteur. Sans lanceur (les essais, un outil sans ecran), startTutorial
// rend NoLauncher.

#include "help/Tutorial.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace help {

struct TutorialStart {
    std::string variant;        // vide : la variante par defaut (@variante)
    std::size_t step = 0;       // l'etape d'ouverture (0 = la premiere)
    bool aTry = false;          // "Essayer" : ouvrir sur l'A toi de cette etape
    bool paused = false;        // "Me montrer" : ouvrir en pause sur l'etape
    std::string returnTopic;    // Quitter : le sujet du centre d'aide ou revenir
    // 1.11.2 (R1112-4) : ce que le lecteur dit en ouvrant, rempli par startTutorial quand "Essayer"
    // ne peut pas ouvrir sur l'etape demandee (elle n'a pas d'A toi) : vide sinon.
    std::string notice;
};

// 1.11.2 (R1112-4) : l'A toi le plus proche de l'etape `step` : elle-meme, sinon la premiere
// apres elle qui en a un, sinon la derniere avant. Aucune : nullopt.
[[nodiscard]] std::optional<std::size_t> nearestATry(const CompiledTutorial& c, std::size_t step);
// Le mot du lecteur quand "Essayer" ouvre ailleurs que demande (aTryStep : l'etape ouverte,
// nullopt : le tutoriel n'a aucun A toi, il montre les gestes).
[[nodiscard]] std::string aTryNotice(std::optional<std::size_t> aTryStep, std::size_t asked);

enum class StartResult { Started, Unknown, Broken, NoLauncher, Refused };

// Inscrit un tutoriel (le texte d'un .tuto). Rend l'id, vide si le texte ne se lit
// pas (les problemes dans *problems). Un id deja inscrit est remplace.
std::string registerTutorialText(std::string_view text, std::vector<TutorialProblem>* problems = nullptr);
// Inscrit chaque .tuto d'un dossier ; rend le nombre de tutoriels inscrits.
int registerTutorialDir(const std::filesystem::path& dir, std::vector<std::string>* errors = nullptr);
void clearTutorials();

// Les tutoriels ecrits, embarques dans l'appli : src/help/TutorialTexts.cpp, GENERE par
// tools/tutoriels/generer_tutoriels.py depuis tools/tutoriels/*.tuto (le meme lecteur les lit).
struct EmbeddedTutorial {
    const char* file;   // objet-vanne.tuto
    const char* text;   // le fichier, tel quel
};
const std::vector<EmbeddedTutorial>& embeddedTutorials();
// Inscrit les tutoriels embarques ; un fichier inscrit ensuite (registerTutorialDir) passe devant.
int registerEmbeddedTutorials(std::vector<std::string>* errors = nullptr);

// Les tutoriels inscrits, dans l'ordre d'inscription.
[[nodiscard]] std::vector<const Tutorial*> tutorials();
// Par l'id, sinon par un @sujet. Nul : aucun.
[[nodiscard]] const Tutorial* findTutorial(std::string_view key);

// L'appli : ouvre le bac a sable et le lecteur. Rend false si elle refuse (un
// tutoriel est deja en cours, pas d'ecran).
using TutorialLauncher = std::function<bool(const Tutorial&, const TutorialStart&)>;
void setTutorialLauncher(TutorialLauncher launcher);

// LE POINT D'ENTREE de T2 : trouve le tutoriel de la cle (l'ecrit, sinon le deduit du
// sujet : tutorialForTopic), verifie qu'il se compile pour la variante, puis le confie
// au lanceur.
StartResult startTutorial(std::string_view key, std::string_view variant = {}, TutorialStart options = {});
[[nodiscard]] const char* startResultName(StartResult r);

// ---- Un tutoriel pour chaque sujet (CONCEPTION-T1 section 5, tranche 9) ---------------
// Qui fait quoi, sans dependre du code des autres :
//  - le centre d'aide (T2) INSCRIT ses sujets : setTopics (un TopicInfo par sujet de
//    l'index, rempli depuis help::center) ; il AFFICHE le compteur : countTutorials() ;
//    sa carte et son lanceur passent par tutorialForTopic / startTutorial(cle du sujet) ;
//  - les tutoriels deduits (T3) : help::deduceTutorial(const TopicInfo&) -> le texte .tuto,
//    BRANCHE par setTutorialDeducer(&help::deduceTutorial) au demarrage de l'appli ;
//  - T1 : le registre, le cache des deduits, le compteur, le verificateur (--verifier-sujets).
// Un tutoriel ecrit a la main pour le sujet (son id, ou @sujet <cle>) passe toujours devant.

// Ce qu'est un sujet (la table "Etapes deduites" de la maquette, NOTES.md section 2).
enum class TopicKind {
    Object,      // un objet de l'IHM : name = le type (Vanne), variants = ses variantes
    Guide,       // une page du guide : places = @lieux, headings = les intertitres ###
    Macro,       // une macro : name = son nom
    Block,       // un bloc DFB : name = son nom
    DataType,    // un type DDT : name = son nom
    Expression,  // un type d'expression : name = le type (couleur, texte, visible...)
    Special,     // les pages speciales (raccourcis, notes de version, signaler)
    Other
};
// "objet", "guide", ... (le tableau du verificateur). En ligne (integration I111) : le
// deducteur de T3 (xpg_hmi) s'en sert sans lier xpg_help.
[[nodiscard]] inline const char* topicKindName(TopicKind k) {
    switch (k) {
    case TopicKind::Object: return "objet";
    case TopicKind::Guide: return "guide";
    case TopicKind::Macro: return "macro";
    case TopicKind::Block: return "bloc";
    case TopicKind::DataType: return "type";
    case TopicKind::Expression: return "expression";
    case TopicKind::Special: return "page";
    case TopicKind::Other: return "autre";
    }
    return "?";
}

// Ce que le deducteur sait d'un sujet. Rempli par qui tient l'index (T2), sans lui.
// LA reference unique (decision du 02/10) : le deducteur de T3 (src/hmi/HmiTutorialDeduce)
// la lit telle quelle.
struct TopicInfo {
    std::string key;                     // la cle du sujet dans le centre (sujet:<cle>, @sujet)
    std::string title;                   // le titre affiche
    TopicKind kind = TopicKind::Other;
    // ce que le sujet decrit, tel que l'utilisateur le lit : Object, le nom francais de la
    // tuile de la bibliotheque ("Vanne") ; la macro, le bloc, le type DDT, le type d'expression
    std::string name;
    std::vector<std::string> variants;   // Object : ses variantes, dans l'ordre de la bibliotheque
    std::vector<std::string> places;     // Guide : ou on le trouve dans l'appli (@lieux)
    std::vector<std::string> headings;   // Guide : les intertitres ### de la page (le deducteur en garde 5)
    std::string body;                    // le texte de la page, si le deducteur en a besoin (sinon vide)

    // ---- Ajoutes a l'integration (I111, 02/10) pour le deducteur de T3 ; vides s'ils ne servent pas.
    std::string objectType;              // Object : le genre interne (@objet du guide : "Valve", "Button") ; name reste "Vanne"
    std::string summary;                 // une phrase : ce qu'on lit en premier
    std::vector<std::pair<std::string, std::string>> properties;   // Object : (libelle de l'inspecteur, son texte)
    std::string place;                   // Guide : la CIBLE de la ligne de l'arbre ("arbre:IHM/Vues"), tiree de places
    std::vector<std::string> headingTexts;   // Guide : le premier paragraphe sous chaque intertitre (l'ordre de headings)
    std::vector<std::pair<std::string, std::string>> fields;       // Macro, Block, DataType : champs, broches, membres (nom, type)
    // Expression (la table hmi::exprguide) : le bouton de la page des expressions, la syntaxe, les
    // operateurs, le premier exemple et sa legende, la premiere erreur courante (faux, juste,
    // pourquoi) et ce que la case attend.
    std::string button, syntax, operators, example, exampleCaption, wrong, right, reason, wanted;
};

// Inscrit les sujets du centre ; remplace la liste et vide le cache des deduits.
void setTopics(std::vector<TopicInfo> topics);
[[nodiscard]] const std::vector<TopicInfo>& topics();
[[nodiscard]] const TopicInfo* findTopic(std::string_view key);

// T3 : ecrit le texte .tuto d'un sujet (vide : il ne sait pas faire ce sujet). Branche au
// demarrage ; vide le cache des deduits. Sans deducteur, un sujet sans tutoriel ecrit n'en a pas.
using TutorialDeducer = std::function<std::string(const TopicInfo&)>;
void setTutorialDeducer(TutorialDeducer deducer);
[[nodiscard]] bool hasTutorialDeducer();

// Le tutoriel d'un sujet : l'ecrit (findTutorial : id ou @sujet), sinon le deduit, lu par
// le MEME lecteur (Tutorial::parse) et garde en cache (un appel suivant rend le meme ; la
// cle est ajoutee a ses topics). Nul : ni ecrit ni deduit, ou le texte deduit ne se lit pas
// (*fault dit pourquoi : "ligne 4 : ..."). Les adresses restent valables jusqu'a clearTutorials.
[[nodiscard]] const Tutorial* tutorialForTopic(std::string_view key, std::string* fault = nullptr);
[[nodiscard]] bool isDeducedTutorial(const Tutorial* t);
// Le texte deduit d'un sujet, tel quel (--tutoriel-texte <sujet>) ; vide sans deducteur.
[[nodiscard]] std::string deducedTutorialText(std::string_view key);

// Un sujet attend-il un tutoriel ? Non pour les notes de version (TopicKind::Special, cle
// "notes-<version>", CenterIndex.hpp) : decision du chef du 03/10 (12). Leur page ne montre
// pas "Regarder le tutoriel" (T2) et le compteur ne les compte pas. Oui pour tous les autres,
// y compris Raccourcis et Signaler (pages speciales qui ont un tutoriel ecrit, tools/tutoriels/).
[[nodiscard]] bool tutorialExpected(const TopicInfo& t);

// Le compteur "Tutoriels prets : ready / total" du centre (T2). Un sujet est PRET quand
// tutorialForTopic rend un tutoriel qui se compile sans probleme pour chacune de ses
// variantes (au moins une etape). Calcule a la demande : a garder, pas a refaire a chaque image.
struct TutorialCount {
    int ready = 0;     // les sujets prets
    int total = 0;     // les sujets inscrits (setTopics) qui attendent un tutoriel (tutorialExpected)
    int written = 0;   // parmi les prets : ecrits a la main
    int deduced = 0;   // parmi les prets : deduits
    std::vector<std::string> missing;   // les cles des sujets sans tutoriel
    std::vector<std::string> broken;    // "<cle> : <premiere faute>" (le texte ne se lit pas, ou une variante ne se compile pas)
    std::vector<std::string> excluded;  // hors du compte : les sujets qui n'attendent pas de tutoriel (les notes de version)
};
[[nodiscard]] TutorialCount countTutorials();

} // namespace help
