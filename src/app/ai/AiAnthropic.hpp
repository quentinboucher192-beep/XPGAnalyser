// =============================================================================
//  app/ai/AiAnthropic.hpp - l'Assistant IA : le fournisseur Anthropic (Claude)
// -----------------------------------------------------------------------------
//  L'API MESSAGES EN HTTP DIRECT (le logiciel est en C++ : pas de SDK).
//
//    POST <adresse>/v1/messages      content-type: application/json,
//                                    x-api-key: <cle>, anthropic-version
//    {"model", "max_tokens", "stream": true, "system", "tools", "messages",
//     "output_config": {"effort"} (seulement s'il est choisi)}
//
//  "system" part en deux blocs de texte quand la requete les donne (promptParts) :
//  le stable (consignes, registre, connaissances) marque cache_control
//  "ephemeral" - le fournisseur le garde en cache d'une demande a l'autre -, puis
//  le dynamique (le detail des genres utiles, le contexte de la vue).
//
//  Rien d'autre dans le corps : ni temperature, ni thinking (les modeles recents
//  pensent d'eux-memes), ni tool_choice force (refuse : la consigne demande
//  l'appel de l'outil), ni "prefill" (le dernier tour est toujours celui de
//  l'utilisateur). Un relais d'entreprise recoit le meme corps.
//
//  LES TOURS. Ceux de l'utilisateur portent, dans l'ordre : le resultat de
//  l'appel d'outil du tour precedent (EN TETE : l'API l'exige), les pieces
//  jointes (PNG/JPG en image, PDF en document, TXT/CSV/JSON/XML en texte avec
//  leur nom ; les autres : une ligne qui dit qu'elles ne sont pas lues), puis le
//  texte. Ceux de l'assistant : son texte, puis son appel d'outil.
//
//  LE FLUX (SSE) devient des ProviderEvent (AiProvider.hpp) : les etats
//  (connexion, reflexion, ecriture, operations), le texte au fil de l'eau,
//  l'appel d'outil COMPLET (les morceaux input_json_delta concatenes, puis lus
//  strictement), la fin. Un appel d'outil n'est rendu qu'a la fin du message :
//  coupe par max_tokens, ou un refus (refusal), il n'est jamais propose.
//
//  LE FIL DE TRAVAIL. start() rend la main tout de suite ; le fil construit le
//  corps (il lit les pieces jointes), envoie, lit le flux et pousse les
//  evenements dans un tampon protege ; poll() les prend a chaque image. Arreter
//  (cancel) ferme la connexion : un Done "arrete" suit. Les erreurs 429, 529 et
//  5xx (et une surcharge annoncee dans le flux avant tout texte) sont
//  REESSAYEES deux fois au plus, avec une attente croissante (ou celle que le
//  service demande : retry-after), sur le fil ; l'etat "reprise" le dit.
//
//  LA LISTE DES MODELES (GET <adresse>/v1/models, gratuite) sert a "Lister les
//  modeles" et a "Tester la connexion" : ModelQuery, sur un fil aussi.
//
//  AUCUN MODELE EN DUR : le modele est celui des parametres. La cle API ne va
//  que dans l'en-tete x-api-key (jamais dans un message, ni dans un journal).
// =============================================================================
#pragma once

#include "AiHttp.hpp"
#include "AiProvider.hpp"
#include "AiSse.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace app::ai {

inline constexpr std::string_view kAnthropicBase = "https://api.anthropic.com";
inline constexpr std::string_view kAnthropicVersion = "2026-10-21";

// L'adresse des messages et celle des modeles, d'apres l'adresse des parametres (vide : celle
// du fournisseur). Une adresse sans chemin : <adresse>/v1/messages ; une qui finit par
// /v1/messages : telle quelle ; un autre chemin : un prefixe (<adresse>/v1/messages). Faux et
// `why` : l'adresse est refusee (parseUrl : http:// seulement vers ce poste).
bool anthropicUrls(std::string_view endpoint, std::string& messages, std::string& models, std::string* why = nullptr);
// Les en-tetes de toute requete (une cle vide : pas de x-api-key, un relais la detient).
[[nodiscard]] HttpHeaders anthropicHeaders(const std::string& apiKey);

// ---- les pieces jointes ---------------------------------------------------------------
inline constexpr std::uintmax_t kMaxImageBytes = 5ull * 1024 * 1024;       // par image
inline constexpr std::uintmax_t kMaxDocumentBytes = 20ull * 1024 * 1024;   // par PDF
// 1.20.0 : un texte n'a plus de limite fixe (avant : coupe a 200 Ko) - il part en parties, a la mesure de la fenetre
// du modele (AiTextParts.hpp) ; kAnthropicWindow : la fenetre supposee quand la liste des modeles ne l'a pas dite.
inline constexpr int            kAnthropicWindow = 200000;
inline constexpr std::uintmax_t kMaxRequestBytes = 28ull * 1024 * 1024;    // le corps entier (la limite de l'API : 32 Mo)

struct AttachmentPlan {
    enum class As : std::uint8_t { Image, Document, Text, NotRead, TooBig };
    As          as{As::NotRead};
    std::string mediaType;   // ce qui part : image/png, image/jpeg, application/pdf, text/plain
    std::string note;        // en francais, quand elle ne part pas entiere (vide : elle part)
};
// D'apres le nom et la taille (pas le disque) : ce que deviendra la piece dans la requete.
[[nodiscard]] AttachmentPlan planAttachment(const ProviderAttachment&);
// Le type d'un fichier d'apres son extension ("image/png", "text/csv"... ; inconnu : application/octet-stream).
[[nodiscard]] std::string mediaTypeOf(std::string_view fileName);

// ---- la requete ------------------------------------------------------------------------
// Le corps JSON de POST /v1/messages (il lit les pieces jointes sur le disque). `notes` :
// celles qui ne sont pas parties entieres, en francais.
[[nodiscard]] std::string anthropicBody(const ProviderRequest&, std::vector<std::string>* notes = nullptr);

// ---- les erreurs -----------------------------------------------------------------------
struct ApiError {
    int         status{0};      // le code HTTP (deduit du type pour une erreur du flux)
    std::string type;           // "overloaded_error"...
    std::string message;        // celui du service (anglais), coupe
    std::string text;           // pour l'utilisateur, en francais
    bool        retryable{false};
};
// Une reponse d'erreur (son code, son corps {"type":"error","error":{...}}) ; status 0 : une
// erreur arrivee dans le flux (event: error). `model` : le nom a citer pour un 404.
[[nodiscard]] ApiError apiError(int status, std::string_view body, std::string_view model);

// ---- le flux ---------------------------------------------------------------------------
// Les octets de la reponse (SSE) -> les ProviderEvent, sans fil ni reseau.
class AnthropicStream {
public:
    void feed(std::string_view chunk);
    // La connexion s'est terminee sans erreur HTTP : sans message_stop, c'est une coupure.
    void end();
    // Les evenements prets (sans l'erreur finale : error()).
    [[nodiscard]] std::vector<ProviderEvent> take();
    [[nodiscard]] bool emitted() const noexcept { return emitted_; }    // du texte ou un appel d'outil
    [[nodiscard]] bool finished() const noexcept { return finished_; }  // le Done est pret
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const ApiError& error() const noexcept { return error_; }
    void setModel(std::string model) { model_ = std::move(model); }

private:
    struct Block {
        std::string type, id, name, json, start;   // json : les morceaux ; start : l'entree du debut
        bool        stopped{false};
    };
    void handle(const SseEvent&);
    void status(const char* s);

    SseReader                  sse_;
    std::map<int, Block>       blocks_;
    std::vector<ProviderEvent> out_, tools_;
    std::string                stopReason_, phase_, model_;
    bool                       stopped_{false}, finished_{false}, failed_{false}, emitted_{false};
    ApiError                   error_;
    ProviderUsage              usage_;   // 1.16.0 : les jetons (message_start, message_delta) - un evenement Usage
};

// ---- le fournisseur ----------------------------------------------------------------------
class AnthropicProvider final : public Provider {
public:
    struct Options {
        int    maxRetries{2};       // nouvelles tentatives au plus (429, 529, 5xx)
        double firstDelay{2.0};     // secondes avant la premiere ; x3 ensuite
        double factor{3.0};
        double maxRetryAfter{60.0}; // un retry-after plus long : pas de nouvelle tentative, le dire
        int    connectTimeout{20};
    };
    AnthropicProvider();
    explicit AnthropicProvider(Options o);
    ~AnthropicProvider() override;

    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ProviderRequest request, std::string* why = nullptr) override;
    [[nodiscard]] std::vector<ProviderEvent> poll() override;
    void cancel() override;
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    void run(ProviderRequest request, std::string url);
    void push(ProviderEvent e);
    bool waitFor(double seconds);   // faux : Arreter pendant l'attente

    Options                     opt_;
    std::thread                 thread_;
    mutable std::mutex          m_;
    std::condition_variable     cv_;
    std::vector<ProviderEvent>  events_;
    std::shared_ptr<HttpClient> http_;
    std::atomic<bool>           busy_{false};
    std::atomic<bool>           cancelled_{false};
};

// ---- la liste des modeles ------------------------------------------------------------------
struct ModelInfo {
    std::string id, name;   // "claude-...", le nom lisible (display_name)
    // 1.16.0 (a la fin) : la fenetre de contexte que le service dit (Anthropic : max_input_tokens ;
    // OpenRouter : context_length...) ; 0 : il ne la dit pas.
    long long   contextTokens{0};
};
struct ModelsResult {
    bool                   ok{false};
    std::vector<ModelInfo> models;
    std::string            error;   // en francais
    int                    httpStatus{0};
};
// Le corps de GET /v1/models (son code) -> la liste.
[[nodiscard]] ModelsResult parseModels(int status, std::string_view body);

class ModelQuery {
public:
    ModelQuery() = default;
    ~ModelQuery();
    // Faux et `why` : deja en cours, adresse refusee, pas de transport.
    bool start(std::string endpoint, std::string apiKey, int timeoutSeconds, std::string* why = nullptr);
    // 1.16.0 : GET `url` avec ces en-tetes (un service compatible OpenAI : <adresse>/models, sa cle en
    // Authorization) ; `openai` : la liste se lit au format OpenAI (parseOpenAIModels, AiOpenAI.hpp).
    bool startUrl(std::string url, HttpHeaders headers, int timeoutSeconds, bool openai, bool allowPrivateHttp, std::string* why = nullptr);
    // Le resultat, une fois (nullopt : pas encore, ou deja pris).
    [[nodiscard]] std::optional<ModelsResult> poll();
    void cancel();
    [[nodiscard]] bool busy() const noexcept { return busy_; }

private:
    std::thread                 thread_;
    std::mutex                  m_;
    std::optional<ModelsResult> result_;
    std::shared_ptr<HttpClient> http_;
    std::atomic<bool>           busy_{false};
};

} // namespace app::ai
