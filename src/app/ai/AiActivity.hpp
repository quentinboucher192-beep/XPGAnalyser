// =============================================================================
//  app/ai/AiActivity.hpp - l'Assistant IA : ce que fait l'IA, a l'instant, et ce que ca coute (1.22.6)
// -----------------------------------------------------------------------------
//  LA LIGNE D'ACTIVITE. Sous les points de la reponse en cours, une ligne dit ce qui se passe A L'INSTANT, d'apres
//  les evenements du fournisseur (AiProvider.hpp) et rien d'autre :
//    << Envoi de la demande (env. 48 k jetons de contexte)... >>,
//    << L'IA reflechit... env. 1 240 jetons de reflexion (0:12) >> (le compteur monte quand le service envoie le texte de
//       la reflexion ; sinon le temps seul : le service dit ses jetons a la fin),
//    << L'IA ecrit sa reponse... 320 mots >>,
//    << L'IA ecrit la proposition : operation 12 (creer Voyant_P3 sur Vue_Pompes)... >> (l'entree de l'outil lue PENDANT
//       qu'elle s'ecrit : InputReader),
//    << L'IA lit le classeur Liste_ES.xlsx (outil lignes_classeur, lignes 200 a 399)... >> (un outil repondu sur ce PC),
//    << Attente du service (limite : nouvelle tentative dans 8 s)... >>...
//  et les dernieres etapes, avec leur duree (<< Demande envoyee : 48 k jetons de contexte (dont 41 k en cache) . 1,2 s >>).
//
//  LE BILAN PAR ACTION. Une reponse = un ou plusieurs APPELS au service (un outil repondu sur ce PC, puis la suite ; une
//  lecture a part d'une piece jointe ; la reponse coupee d'une reprise en parties) : pour chacun, son but, ses jetons
//  d'entree (dont le cache lu, ecrit), de sortie (dont la reflexion), sa duree. Le reel du service des qu'il le dit ; entre
//  deux, une ESTIMATION d'apres le texte recu (un jeton pour quatre octets), marquee << env. >> et remplacee par le reel.
//  Aucun prix : le logiciel ne connait pas celui des modeles (les jetons seulement). Le bilan est garde avec le message
//  (Message::extra << bilan >>) ; un message d'avant (sans bilan) n'en montre pas.
//
//  Sans horloge : le temps est celui qu'on passe (la seconde de l'application, Assistant::tick) - les essais sont
//  reproductibles. Rien ne bloque : quelques octets lus par evenement, la ligne refaite au plus quatre fois par seconde
//  (kRefresh, par le panneau).
// =============================================================================
#pragma once

#include "AiJson.hpp"
#include "AiProvider.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::activity {

inline constexpr double      kRefresh = 0.25;   // la ligne et les compteurs : quatre fois par seconde au plus
inline constexpr std::size_t kHistory = 4;      // les dernieres etapes gardees (montrees sous la ligne)
inline constexpr long long   kBytesPerToken = 4;

// ---- les textes ----------------------------------------------------------------------------------------------------
// "0:42", "12:05", "1:02:03".
[[nodiscard]] std::string clockText(double seconds);
// "0,8 s", "12 s", "1:05".
[[nodiscard]] std::string shortDuration(double seconds);
// "1 240" (l'espace insecable des milliers).
[[nodiscard]] std::string grouped(long long n);
// env. : les jetons de `bytes` octets de texte (au moins 1 pour un texte non vide).
[[nodiscard]] long long estimateTokens(std::size_t bytes);
// env. : les jetons de contexte d'une demande - ses consignes, ses tours, ses outils (les pieces jointes non comptees).
[[nodiscard]] long long requestTokens(const ProviderRequest&);

// ---- l'entree d'un appel d'outil, lue pendant qu'elle s'ecrit ----------------------------------------------------------
// Un lecteur JSON par morceaux (coupes n'importe ou) : les elements commences du tableau `itemsKey` de l'objet racine
// ("operations" de proposer_operations, "etapes" de planifier_mission), et les champs texte du dernier (action, nom, vue,
// objet, variable, id, titre... ; le premier de "objets"). Il ne garde que des textes courts : une entree de 50 Ko se lit
// sans rien copier.
class InputReader {
public:
    explicit InputReader(std::string itemsKey = "operations") : key_(std::move(itemsKey)) {}
    void feed(std::string_view chunk);
    [[nodiscard]] int                                       items() const noexcept { return items_; }
    [[nodiscard]] bool                                      closed() const noexcept { return closed_; }   // le tableau est ferme
    [[nodiscard]] const std::map<std::string, std::string>& last() const noexcept { return last_; }
    [[nodiscard]] std::size_t                               bytes() const noexcept { return bytes_; }

private:
    struct Frame {
        bool        object{true};
        bool        expectKey{true};
        std::string key;      // la cle en cours (un objet)
        int         index{0}; // l'element en cours (un tableau)
    };
    void endString();
    [[nodiscard]] bool inItem() const noexcept;   // la pile est dans un element du tableau suivi (son objet)

    std::string                        key_;
    std::vector<Frame>                 stack_;
    std::map<std::string, std::string> last_;
    std::string                        str_;
    bool                               inStr_{false}, esc_{false}, strKey_{false}, keep_{false}, closed_{false}, over_{false};
    int                                hex_{-1};        // les chiffres d'un \uXXXX qui restent (-1 : aucun)
    unsigned                           code_{0};
    int                                items_{0};
    std::size_t                        bytes_{0};
};
// Une operation de proposer_operations, en clair : "cr\xC3\xA9" "er Voyant_P3 sur Vue_Pompes" (vide : rien de lisible encore).
[[nodiscard]] std::string operationText(const std::map<std::string, std::string>& fields);

// ---- les appels au service, le bilan ---------------------------------------------------------------------------------
struct Call {
    std::string   purpose;              // "Proposition", "Lecture du classeur Liste_ES.xlsx (lignes_classeur)"...
    ProviderUsage usage;                // le reel, tel que le service le dit (le dernier recu pour cet appel)
    long long     estimate{0};          // env. : la sortie d'apres ce qui est arrive (texte, reflexion, entree d'outil)
    long long     thinking{0};          // la reflexion (comprise dans la sortie)
    bool          thinkingExact{false}; // dite par le service (sinon : env.)
    bool          outputKnown{false};   // la sortie est celle que le service a dite a la fin (sinon : env.)
    bool          aside{false};         // une lecture a part d'une piece jointe (1.20.0)
    bool          carried{false};       // la reponse coupee d'une reprise en parties (deja comptee au cumul)
    double        seconds{0};
    [[nodiscard]] long long input() const noexcept { return usage.input + usage.cacheRead + usage.cacheWrite; }
    [[nodiscard]] long long output() const noexcept { return outputKnown ? usage.output : (usage.output > estimate ? usage.output : estimate); }
    [[nodiscard]] long long total() const noexcept { return input() + output(); }
    [[nodiscard]] bool      estimated() const noexcept { return !outputKnown && output() > 0; }
};
struct Bill {
    std::vector<Call> calls;
    double            seconds{0};   // la reponse, de l'envoi a la fin
    [[nodiscard]] bool      empty() const noexcept { return calls.empty(); }
    [[nodiscard]] long long total() const noexcept;
    [[nodiscard]] bool      estimated() const noexcept;
};
// Message::extra << bilan >> : {"format": 1, "duree": s, "appels": [{"but", "entree", "cacheLu", "cacheEcrit", "sortie",
// "reflexion", "reflexionExacte", "estime", "aPart", "dejaCompte", "duree"}]}.
[[nodiscard]] json::Value         toJson(const Bill&);
// Le bilan garde avec un message (son extra) ; nul : aucun (un message d'avant la 1.22.6, une reponse sans appel).
[[nodiscard]] std::optional<Bill> billFrom(const json::Value& extra);
// "48 k jetons \xC2\xB7 0:42 \xC2\xB7 3 appels" ; une estimation : "env. 48 k jetons...".
[[nodiscard]] std::string              billLine(const Bill&);
// Le detail : une ligne par appel, puis le total.
[[nodiscard]] std::vector<std::string> billDetail(const Bill&);

// ---- l'activite ----------------------------------------------------------------------------------------------------
struct Step {
    std::string text;      // ce qui s'est fait (sans la duree)
    double      seconds{0};
};
struct Snapshot {
    int               message{-1};      // le message de la conversation ouverte sous lequel elle se dit
    std::string       context;          // "Mission : S3b Configurations de gaz", "Correction 1/2 (Compiler : 2 erreurs)" ; vide : rien
    std::string       line;             // ce qui se passe a l'instant ; vide : rien a dire
    std::vector<Step> history;          // les dernieres etapes, la plus recente d'abord
    bool              keepDetail{true}; // ce que l'indicateur dit en plus (Assistant::statusDetail) reste utile (une nouvelle tentative)
    bool              working{true};    // un travail en cours (le trait avance)
    [[nodiscard]] bool empty() const noexcept { return line.empty(); }
};

// Le suivi d'une reponse d'un fournisseur par le reseau (Assistant : la generation en cours).
class Tracker {
public:
    struct Setup {
        std::string context;              // la mission, la correction (vide : rien)
        std::string purpose;              // le but du dernier appel ("Plan de mission"...) ; vide : decide a la fin
        long long   contextEstimate{0};   // env. : les jetons de contexte envoyes
        bool        textTool{false};      // la proposition s'ecrit en texte (un bloc ```json)
    };
    void start(Setup s, double now);
    // Un evenement du fournisseur. Vrai : les etapes ou les appels ont change (la mise en page suit).
    bool observe(const ProviderEvent& e, double now);
    // La fin de la reponse : les appels fermes, leurs buts decides (`proposed` : une proposition est arrivee).
    void finish(double now, bool proposed);
    // Une reprise en parties : les appels de la reponse coupee, montres avec celle-ci (deja comptes).
    void carry(const Bill& cut);
    [[nodiscard]] bool started() const noexcept { return start_ >= 0; }

    [[nodiscard]] Snapshot      snapshot(double now) const;
    [[nodiscard]] Bill          bill(double now) const;
    [[nodiscard]] ProviderUsage realUsage() const;   // le reel dit par le service (sans les lectures a part ni la reponse coupee)
    [[nodiscard]] long long     liveTokens() const;  // le reel et les estimations, lectures a part comprises (pas la reponse coupee)
    [[nodiscard]] bool          estimating() const;  // une part estimee
    [[nodiscard]] long long     contextNow() const;  // l'entree du dernier appel que le service a dite (0 : pas encore)
    [[nodiscard]] std::vector<Step> history() const;

private:
    enum class Kind : std::uint8_t { Sending, Retry, Length, Reading, Thinking, Writing, Tool, LocalTool };
    struct StepState {
        Kind        kind{Kind::Sending};
        double      start{0}, end{-1};
        int         call{0};
        std::string detail;            // Reading : le texte ; Retry : la cause ; Length : la longueur ; LocalTool : ce que l'outil dit
        std::string tool, after;       // Tool, LocalTool : l'outil ; Sending : l'outil dont le resultat part
        std::string doing, done;       // LocalTool : les phrases (en cours, faite)
        double      until{-1};         // Retry : la nouvelle tentative
        int         http{0};
        std::size_t words{0};          // Writing
        std::size_t thinking{0};       // Thinking : les octets de reflexion recus
        int         items{0};          // Tool : les elements commences (operations, etapes)
        bool        closed{false};
        std::map<std::string, std::string> fields;   // Tool : le dernier element
    };
    struct CallState {
        Call        c;
        double      start{0}, end{-1};
        std::size_t text{0}, thinking{0}, tool{0};   // les octets recus
        bool        thinkingSeen{false};
        int         thinkingSteps{0};
    };
    void        begin(Kind k, double now);
    CallState&  call(int k, double now);
    void        closeCall(CallState& c, double now);
    void        feedTool(std::string_view chunk);
    [[nodiscard]] std::string present(const StepState& s, double now) const;
    [[nodiscard]] std::string past(const StepState& s) const;
    [[nodiscard]] Call        callOf(const CallState& c, double now) const;

    Setup                  setup_;
    double                 start_{-1};
    StepState              cur_;
    std::deque<StepState>  done_;
    std::vector<CallState> calls_;
    std::vector<Call>      carried_;
    std::vector<Call>      aside_;
    std::optional<InputReader> reader_;
    std::string            readerTool_;
    std::string            reading_;        // la derniere lecture a part dite (son but)
    double                 readingAt_{-1};
    int                    current_{0};     // l'appel en cours
    bool                   inWord_{false}, fenced_{false};
    std::string            tail_;           // la fin du texte (une balise ``` coupee entre deux morceaux)
    bool                   finished_{false};
    double                 finishedAt_{0};
};

} // namespace app::ai::activity
