#pragma once
// Le moteur des tutoriels (1.11, D2 et D3) : un tutoriel est une DONNEE.
//
// Ce fichier : le format .tuto (voir v111/agents/CONCEPTION-T1.md, section 1),
// son lecteur et sa compilation par variante (la frise : l'instant de chaque
// geste et de chaque etape). Il ne sait pas qu'il existe un ecran : le lecteur
// (TutorialPlayer) parle a une scene abstraite, et les essais tournent sans X.
//
// Un fichier texte UTF-8, une commande par ligne, lu comme une session de
// ScriptRunner : memes mots ("entre guillemets" fait un mot, \" et \\ s'y
// echappent), memes commentaires #, memes noms de gestes (survol, clic,
// glisser, texte, touche, attendre), plus encadrer, dire et simuler.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace help {

// Prepare : une commande de preparation du bac a sable (section @avant seulement) :
// vue, poser, regler. Elle ne s'anime pas : la scene l'applique au projet du bac.
enum class GestureKind { Move, Click, Drag, Type, Key, Spot, Say, Wait, Run, Prepare };

// Un geste d'une etape. Les durees sont a 1x, en millisecondes.
struct Gesture {
    GestureKind kind = GestureKind::Wait;
    std::string target;    // la cible, "genre:nom" ou une zone nommee ; vide si aucune
    std::string target2;   // glisser : la cible du depot
    std::string text;      // texte : ce qui est tape ; touche : la touche ; dire : la bulle
    bool doubleClick = false;
    bool rightClick = false;
    int durationMs = 0;
    int startMs = 0;       // depuis le debut de l'etape (rempli par compile)
    int line = 0;          // la ligne du fichier
    std::vector<std::string> args;  // Prepare : la commande et ses mots ("poser", "Vanne", "400,262", "V_201")
};

// Une condition "<chemin> <op> <valeur>", op parmi = <> < <= > >= ~ (contient).
struct TutorialCheck {
    std::string path, op, value;
    std::string reason;    // @presque : ce que dit la bulle jaune ; {valeur} y cite la valeur lue
    int line = 0;
};

// "A toi" : l'utilisateur fait l'etape lui-meme, le tutoriel verifie.
struct TutorialATry {
    std::string instruction;           // @atoi
    std::string target;                // @cible (defaut : la cible du dernier geste)
    // 1.11.2 (le LISEZ-MOI de la 1.11.0 : « l'A toi ne te montre pas la tuile a glisser », la
    // bibliotheque revenue en haut apres la remise en place) : ce que l'A toi montre a son debut,
    // amene a l'ecran (defile, deplie) et encadre, sans clic. @montrer <cible> ; defaut : la
    // premiere tuile des gestes de l'etape (biblio:..., variante:...), sinon rien.
    std::string show;
    std::vector<TutorialCheck> checks; // @verifier : toutes vraies
    std::vector<TutorialCheck> almost; // @presque : une erreur reconnue
    std::string bravo;                 // @bravo (defaut : Bravo !)
    std::string onOkVariantFrom;       // @onok variante=<chemin>
};

struct TutorialStep {
    std::string title;
    std::string bubble;                // les paragraphes separes par \n
    std::vector<Gesture> gestures;
    std::optional<TutorialATry> aTry;
    int number = 0;                    // le numero joue, 1..n (apres le filtre des variantes)
    int writtenNumber = 0;             // le numero ecrit dans le fichier
    int startMs = 0;                   // depuis le debut du tutoriel
    int durationMs = 0;
    int line = 0;
};

struct TutorialProblem {
    int line = 0;
    std::string message;
};

// Un tutoriel compile pour une variante : ce que le lecteur joue.
struct CompiledTutorial {
    std::string id, title, variant, sandbox, view;
    std::vector<std::string> variants;
    // @avant : ce qui prepare le bac a sable (les vues, les objets), joue sans
    // animation a chaque remise a neuf, avant l'etape 1. Hors de la frise.
    std::vector<Gesture> before;
    std::vector<TutorialStep> steps;
    int totalMs = 0;
    std::vector<TutorialProblem> problems;  // une etape sans bulle, sans geste, un A toi sans @verifier...

    // L'etape qui contient l'instant ms (la derniere si ms depasse la fin).
    [[nodiscard]] std::size_t stepAt(double ms) const;
};

// Un filtre de variantes : [a, b] (celles-ci) ou [!a] (toutes sauf a). Vide : toutes.
struct VariantFilter {
    std::vector<std::string> names;
    bool negate = false;
    [[nodiscard]] bool accepts(std::string_view variant) const;
};

struct Tutorial {
    std::string id, title;
    std::vector<std::string> topics;    // @sujet, plusieurs possibles
    std::vector<std::string> variants;  // @variantes
    std::string defaultVariant;         // @variante, sinon la premiere
    std::string sandbox = "demo-ihm";   // @bac
    std::string view;
    int msPerLetter = 85;               // @duree-lettre (la maquette : 85 ms par lettre)

    // La source, deja lue, avec le filtre de chaque ligne : compile() filtre.
    struct Item {
        enum class Kind { Bubble, Gesture, ATry, Target, Verify, Almost, Bravo, OnOk, Show };
        Kind kind = Kind::Bubble;
        VariantFilter filter;
        Gesture gesture;
        TutorialCheck check;
        std::string text;
        bool newParagraph = false;      // "::"
        int line = 0;
    };
    struct SourceStep {
        int writtenNumber = 0;
        std::string title;
        VariantFilter filter;
        std::vector<Item> items;
        int line = 0;
    };
    std::vector<SourceStep> source;
    // La section @avant (avant le premier ==) : des gestes et des commandes de
    // preparation (vue, poser, regler), filtrables par [variante] comme le reste.
    std::vector<Item> before;

    // Lit un fichier .tuto. Tout ce qui ne va pas est rendu dans problems, avec
    // sa ligne ; la ligne fautive est sautee, le reste est lu quand meme.
    [[nodiscard]] static Tutorial parse(std::string_view text, std::vector<TutorialProblem>* problems = nullptr);

    // Les etapes de cette variante (vide : la variante par defaut), les lignes
    // [...] filtrees, {variante} remplace, la frise calculee.
    [[nodiscard]] CompiledTutorial compile(std::string_view variant = {}) const;
};

// Les outils du format, exposes pour les essais et pour T3 (les tutoriels deduits).
[[nodiscard]] std::vector<std::string> tutorialWords(std::string_view line);
// "1.5s", "800ms", ou un entier nu = des images a 1/30 s (comme attendre). -1 si illisible.
[[nodiscard]] int parseDurationMs(std::string_view word);
// Le temps de lecture d'une bulle : 45 ms par lettre, de 1,5 s a 6 s.
[[nodiscard]] int readingTimeMs(std::string_view text);
// Les durees de la maquette validee (maquette-1.11.html, compile()) : apres chaque geste, 120 ms
// (0 apres encadrer, 900 apres dire) ; en fin d'etape, 1,8 s pour lire la bulle.
inline constexpr int kGestureGapMs = 120;
inline constexpr int kSayGapMs = 900;
inline constexpr int kStepReadMs = 1800;
[[nodiscard]] int gestureGapMs(GestureKind kind);
[[nodiscard]] std::size_t utf8Letters(std::string_view text);
// 64000 -> "1:04"
[[nodiscard]] std::string formatClock(int ms);
[[nodiscard]] const char* gestureName(GestureKind kind);
// Les commandes de preparation de @avant : vue <nom>, poser <genre> <x,y> [<nom>],
// regler <objet> <propriete> <valeur>.
[[nodiscard]] bool isPrepareCommand(std::string_view word);
// Une cible connue : "genre:nom" avec un genre connu, ou une zone nommee (un mot sans ":").
[[nodiscard]] bool isKnownTarget(std::string_view target);

// La verification d'un "A toi", sans ecran : la scene lit les chemins, ceci compare.
struct CheckOutcome {
    enum class Result { NotYet, Almost, Ok };
    Result result = Result::NotYet;
    std::string message;   // le bravo ou la raison, {valeur} remplace
    std::string value;     // la valeur lue pour la premiere condition
};
using PathReader = std::function<std::optional<std::string>(std::string_view path)>;
[[nodiscard]] bool evaluateCheck(const TutorialCheck& check, std::string_view actual);
[[nodiscard]] CheckOutcome evaluateATry(const TutorialATry& aTry, const PathReader& read);

} // namespace help
