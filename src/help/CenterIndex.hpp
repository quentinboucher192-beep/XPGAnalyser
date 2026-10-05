// =============================================================================
//  help/CenterIndex.hpp - 1.11 (chantier T2) : l'index unique du centre d'aide
// -----------------------------------------------------------------------------
//  UN SEUL CENTRE D'AIDE (D1). Il remplace les quatre ecrans d'avant : l'aide
//  generale (HelpDocument), l'aide de l'IHM (le guide, HmiHelpPane), les macros
//  et les blocs DFB / DDT (libs/index.txt). Ce module dit CE QUE le centre
//  contient et DANS QUEL ORDRE ; l'ecran (app/help) ne fait que le dessiner.
//
//  L'ARBRE : neuf chapitres, dans cet ordre - Demarrer, L'IHM, L'automate
//  (API), Macros, Blocs DFB / DDT, Expressions, Raccourcis, Notes de version,
//  Signaler un probleme.
//
//  LES CLES. Un sujet du guide garde sa cle ("objet-vanne", "raccourcis") :
//  c'est celle que les tutoriels ecrivent dans @sujet. Les autres sont
//  prefixees : "api-<ancre>" (les pages de l'aide generale : "dossiers" et
//  "alarmes" existent aussi dans le guide), "macro-<nom>", "bloc-<nom>",
//  "expr-<type>", "notes-<version>", "page-raccourcis", "page-signaler".
//
//  LES TUTORIELS (T1). Le centre ne depend pas du moteur des tutoriels pour
//  compiler : T1 branche deux fonctions (setTutorialInfoProvider,
//  setTutorialLauncher). Sans elles, la duree est estimee (estimated = vrai)
//  et "Regarder le tutoriel" dit qu'il arrive.
//
//  Pur, sans ecran : les sujets arrivent en SourceTopic, que l'appli remplit
//  depuis le guide, HelpDocument et la bibliotheque (les essais : a la main).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace help {
struct Target;   // HelpIndex.hpp : une destination de l'aide (la navigation, les favoris, les recents)
}

namespace help::keys { struct Shortcut; }
namespace help::notes { struct Note; }

namespace help::center {

enum class Chapter : std::uint8_t { Start, Hmi, Plc, Macros, Blocks, Expressions, Shortcuts, Notes, Report };
inline constexpr std::size_t kChapterCount = 9;

enum class Source : std::uint8_t { Guide, ApiPage, Macro, Block, Expression, ShortcutsPage, NotesPage, ReportPage };

// Le nom d'un chapitre a l'ecran ("D\xC3\xA9marrer", "L'IHM"...).
[[nodiscard]] std::string_view chapterLabel(Chapter);
[[nodiscard]] const std::vector<Chapter>& chapters();

// ---- ce qui entre dans l'index ----------------------------------------------
struct SourceTopic {
    std::string key;        // la cle dans sa source : la cle du guide, l'ancre, le nom du bloc
    std::string title;
    std::string summary;
    std::string group;      // le guide : son chapitre ("Concevoir les vues") ; un bloc : sa categorie ;
                            // une page de l'API : "start" ou "plc" (Demarrer ou L'automate)
    std::string kind;       // "objet" (un @objet du guide), "macro", "dfb", "ddt" ; vide : un sujet
    int         headings{0};   // le nombre d'intertitres (### du guide) : l'estimation du tutoriel
};

struct Inputs {
    std::vector<SourceTopic> guide;     // dans l'ordre du guide
    std::vector<SourceTopic> api;       // les pages de l'aide generale (sans les pages "Nouveautes de...")
    std::vector<SourceTopic> library;   // macros et blocs, kind "macro" / "dfb" / "ddt"
    // Les 11 types d'expression de T3 (hmi::exprguide::all() : key, title,
    // summary) ; vide : la liste ecrite dans CenterIndex.cpp, memes cles.
    std::vector<SourceTopic> expressions;
};

// ---- un sujet du centre -------------------------------------------------------
struct Topic {
    std::string key;        // la cle unique du centre
    Chapter     chapter{Chapter::Start};
    std::string sub;        // le sous-chapitre ("Concevoir les vues", "\xC3\x89quipements") ; vide : aucun
    std::string title;
    std::string summary;
    Source      source{Source::Guide};
    std::string ref;        // la cle dans sa source (cle du guide, ancre, nom du bloc, version)
    std::string kind;       // repris de SourceTopic
    int         headings{0};
};

// ---- les tutoriels : l'interface avec T1 ----------------------------------------
struct TutorialInfo {
    int  durationMs{0};
    int  steps{0};
    bool estimated{true};    // vrai : aucun tutoriel n'est encore fourni, la duree est estimee
    std::vector<std::pair<std::string, int>> stepStarts;   // le titre de chaque etape, son instant (ms)
};

struct TutorialRequest {
    std::string topic;        // la cle du sujet
    std::string variant;      // vide : la premiere variante
    std::size_t step{0};      // 0 : le debut ; n : l'etape n (Me montrer, un clic dans la liste des etapes)
    bool        aTry{false};  // "Essayer" : directement en "A toi"
    bool        paused{false};// "Me montrer" des notes : ouvert en pause
    std::string returnTopic;  // Quitter rappelle le centre sur ce sujet
};

using InfoProvider = std::function<std::optional<TutorialInfo>(std::string_view topicKey)>;
using Launcher     = std::function<bool(const TutorialRequest&)>;

// T1 branche ces deux fonctions au lancement (App). Une fonction vide : retirer.
void setTutorialInfoProvider(InfoProvider);
void setTutorialLauncher(Launcher);
[[nodiscard]] bool hasTutorialLauncher();

// La duree et les etapes du tutoriel d'un sujet : le fournisseur, sinon
// l'estimation (estimated = vrai). Jamais zero etape : chaque sujet a son tutoriel.
[[nodiscard]] TutorialInfo tutorialInfo(const Topic&);
[[nodiscard]] TutorialInfo estimateTutorial(const Topic&);
// "1 min 04", "54 s" : ce que la pastille et la carte ecrivent.
[[nodiscard]] std::string formatDuration(int ms);

// Lance le tutoriel ; faux sans lanceur (ou s'il refuse) : la page dit
// "le tutoriel de ce sujet arrive".
[[nodiscard]] bool launchTutorial(const TutorialRequest&);

// ---- la recherche unique -----------------------------------------------------------
// Les groupes, dans l'ordre de la maquette ; vides : absents du resultat.
enum class GroupKind : std::uint8_t { Hmi, Plc, Macros, Blocks, Expressions, Shortcuts, Notes };
[[nodiscard]] std::string_view groupLabel(GroupKind);
// 4 resultats par groupe, 3 pour les raccourcis et les notes.
[[nodiscard]] std::size_t groupLimit(GroupKind);

struct SearchHit {
    const Topic*           topic{nullptr};      // un sujet (aussi la page des raccourcis, une page de notes)
    const keys::Shortcut*  shortcut{nullptr};   // une ligne de la table des raccourcis
    const notes::Note*     note{nullptr};       // une ligne des notes de version
    std::string            title;               // ce que la ligne ecrit en gras
    std::string            detail;              // en gris, apres
    std::vector<std::pair<std::size_t, std::size_t>> marks;   // dans `title` : [debut, fin) en octets, a surligner
    int                    score{0};
};

struct SearchGroup {
    GroupKind              kind{GroupKind::Hmi};
    std::vector<SearchHit> hits;    // au plus groupLimit(kind), les meilleurs d'abord
    std::size_t            total{0};// tout ce qui a ete trouve dans ce groupe
};

// ---- F1 --------------------------------------------------------------------------
// Ce que les trois F1 d'aujourd'hui savent de l'endroit (le volet de l'IHM, la
// bibliotheque, l'onglet de l'API) : l'ecran remplit ce qu'il sait.
struct F1Place {
    std::string guideTopic;     // le lieu de l'IHM (hmi::guide::topicForPlace) : "objet-vanne"
    std::string libraryEntry;   // le bloc ou la macro (la help::Target de la bibliotheque) : "ST_EQ_Pump"
    std::string apiAnchor;      // l'ancre de l'aide generale de l'onglet de l'API : "sim-debogage"
    std::string word;           // le mot sous le curseur, en dernier
};

// Un sujet du centre en destination de la navigation (TargetKind::Topic).
[[nodiscard]] help::Target topicTarget(std::string_view key);

// ---- l'index --------------------------------------------------------------------
class Index {
public:
    // Les sources -> l'arbre. Les sujets fixes (les 11 types d'expressions, la
    // page des raccourcis, les notes de chaque version de help::notes, la page
    // Signaler) sont ajoutes ici.
    [[nodiscard]] static Index build(const Inputs&);

    [[nodiscard]] const std::vector<Topic>& topics() const noexcept { return topics_; }
    [[nodiscard]] const Topic* find(std::string_view key) const;
    [[nodiscard]] std::vector<const Topic*> ofChapter(Chapter) const;
    [[nodiscard]] std::size_t count(Chapter) const;

    // L'ordre de l'arbre : en bas de la page, les sujets precedent et suivant.
    [[nodiscard]] const Topic* next(std::string_view key) const;
    [[nodiscard]] const Topic* prev(std::string_view key) const;

    // LA RECHERCHE UNIQUE (Ctrl+F) : sans casse ni accents ; un titre qui
    // commence par le terme passe devant ; les groupes vides sont omis. Un
    // terme vide (ou d'espaces) : rien.
    [[nodiscard]] std::vector<SearchGroup> search(std::string_view term) const;

    // F1 (1.11, tranche 2) : la cle du sujet de l'endroit, du plus precis au
    // plus general - le lieu de l'IHM, le bloc ou la macro, la page de l'onglet
    // de l'API, puis le mot (une cle, un titre egal, sinon le premier sujet de
    // la recherche). Vide : rien (le centre s'ouvre sur Demarrer).
    [[nodiscard]] std::string forF1(const F1Place&) const;

    // LA NAVIGATION, LES FAVORIS, LES RECENTS : help::Navigation (HelpSession)
    // telle quelle ; un sujet y est une help::Target de genre Topic
    // ("sujet:<cle>" dans les reglages). keyOf rend la cle d'une cible : un
    // sujet, ou une cible de la bibliotheque d'avant (lib:/par: -> bloc-/macro-,
    // ce qui garde les favoris de la 1.10) ; vide : rien dans l'index.
    [[nodiscard]] std::string keyOf(const help::Target&) const;

    // 1.11.1 (recette R1111-14) : la cle d'un renvoi d'une page du centre -
    // "sujet:<cle>", "topic:<cle>" (les puces « Voir aussi » d'un sujet du
    // guide, faites par HmiHelpPane::compose : la cle du guide est celle du
    // sujet ici) ou la cle seule ; vide : rien dans l'index.
    [[nodiscard]] std::string keyOfLink(std::string_view target) const;

    // Les cles en double rencontrees (vide : aucune) - l'essai l'exige vide.
    [[nodiscard]] const std::vector<std::string>& duplicates() const noexcept { return duplicates_; }

private:
    void add(Topic t);
    std::vector<Topic>       topics_;
    std::vector<std::string> duplicates_;
};

// Les 11 types de la page des expressions : (cle sans "expr-", titre).
[[nodiscard]] const std::vector<std::pair<std::string_view, std::string_view>>& expressionTypes();

} // namespace help::center
