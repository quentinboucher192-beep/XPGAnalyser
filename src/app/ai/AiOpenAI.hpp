// =============================================================================
//  app/ai/AiOpenAI.hpp - l'Assistant IA : un service COMPATIBLE OPENAI
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier FOURNISSEURS le remplit). L'API "chat
//  completions" que parlent Ollama et LM Studio (sur ce PC, gratuits, sans cle)
//  et beaucoup de services en ligne (OpenRouter, Groq, Google Gemini, Mistral...,
//  certains avec une offre gratuite) : POST <adresse>/chat/completions (stream),
//  GET <adresse>/models. Le modele reste une case libre (rien en dur) ; le
//  service propose seulement son adresse.
//
//  LA REQUETE (le meme ProviderRequest que pour Anthropic) :
//    POST <adresse>/chat/completions   content-type: application/json,
//                                      Authorization: Bearer <cle> (seulement
//                                      si une cle est donnee)
//    {"model", "max_tokens", "stream": true, "stream_options": {"include_usage":
//     true} (retire si le service le refuse), "messages", "tools"}
//  Les messages : "system" (les consignes) ; "user" (le texte ; les images en
//  image_url data:base64 ; les textes joints en texte ; un PDF n'est pas lu par
//  ces services : une ligne le dit) ; "assistant" (son texte et ses tool_calls) ;
//  "tool" (le resultat de l'appel precedent, tool_call_id). L'outil
//  proposer_operations part en {"type": "function", "function": {name,
//  description, parameters}}.
//
//  UN MODELE SANS APPEL D'OUTIL (beaucoup de petits modeles gratuits) : le
//  service refuse la demande avec outil (400 "does not support tools"). Avec
//  ProviderRequest::textFallback (Settings::toolMode "auto"), le fournisseur
//  reessaie aussitot SANS outil : les consignes demandent alors la proposition
//  dans un bloc ```json du texte (AiPrompt.hpp : textToolInstructions), lu a la
//  fin de la reponse et rendu comme un appel d'outil (id "texte-...") - la MEME
//  lecture stricte et la meme validation ensuite. L'etat "sans-outil" le dit a
//  l'Assistant (il s'en souvient pour les demandes suivantes). toolsInText : le
//  JSON en texte d'emblee (Settings::toolMode "texte").
//
//  LE FLUX : "data: {...}" (choices[0].delta.content : le texte ;
//  choices[0].delta.tool_calls[i] : id, nom, arguments a CONCATENER par index ;
//  finish_reason : stop, tool_calls, length, content_filter), un dernier morceau
//  avec "usage", puis "data: [DONE]". Une erreur : {"error": {"message"}}.
//
//  LA LIMITE (AiUsage.hpp) : les en-tetes x-ratelimit-* de chaque reponse ; un
//  service sur ce PC (ou sur le reseau de l'usine) : ProviderLimits::local.
//
//  LES ADRESSES : https://, ou http:// vers ce PC (Ollama, LM Studio) ; http://
//  vers une adresse privee de l'usine (10.x, 192.168.x, 172.16-31.x) SEULEMENT
//  sans cle (sinon elle passerait en clair : refuse, et dit).
// =============================================================================
#pragma once

#include "AiAnthropic.hpp"
#include "AiProvider.hpp"
#include "AiSse.hpp"

#include <map>
#include <memory>
#include <string_view>
#include <vector>

namespace app::ai {

// Un service connu : sa cle (Settings::preset), son libelle, son adresse de base (avec /v1...), sur ce PC
// (pas de limite, pas de cle) ou en ligne.
struct ServicePreset {
    std::string_view key, label, endpoint;
    bool             local{false};
    bool             needsKey{true};
};
[[nodiscard]] const std::vector<ServicePreset>& servicePresets();
[[nodiscard]] const ServicePreset* findServicePreset(std::string_view key) noexcept;

// Le fournisseur (makeProvider("openai")) ; nul dans le contrat, le chantier le remplit.
[[nodiscard]] std::unique_ptr<Provider> makeOpenAIProvider();

// ---- 1.16.0 (FOURNISSEURS) : ce qui suit le contrat ---------------------------------------------
// Les adresses d'un service : l'adresse ecrite (vide : celle du service choisi), sans "/chat/completions".
struct OpenAITarget {
    std::string chat, models;   // <base>/chat/completions, <base>/models
    bool        local{false};   // ce PC, ou le reseau de l'usine : pas de limite de service
    bool        privateHttp{false};   // http:// vers une adresse privee (sans cle)
};
// Faux et `why` (en francais) : pas d'adresse, adresse refusee, http:// vers une autre machine avec une cle.
bool openAITarget(std::string_view endpoint, std::string_view preset, bool hasKey, OpenAITarget& out, std::string* why = nullptr);
// Le service est-il sur ce PC (ou le reseau de l'usine) ? (Ollama, LM Studio, une adresse locale.)
[[nodiscard]] bool localService(std::string_view endpoint, std::string_view preset);
// Les en-tetes (une cle vide : pas d'Authorization).
[[nodiscard]] HttpHeaders openAIHeaders(const std::string& apiKey);
// Le corps JSON. `withTools` faux : sans "tools", la proposition demandee en texte (les consignes
// de AiPrompt.hpp ajoutees, les appels precedents ecrits en blocs ```json) ; `streamUsage` :
// stream_options.include_usage.
[[nodiscard]] std::string openAIBody(const ProviderRequest&, bool withTools, bool streamUsage, std::vector<std::string>* notes = nullptr);
// Une reponse d'erreur (son code, son corps : {"error": {"message"}} ou {"error": "..."}).
[[nodiscard]] ApiError openAIError(int status, std::string_view body, std::string_view model);
// Le service refuse-t-il l'outil (un modele sans appel d'outil) ? le champ stream_options ?
[[nodiscard]] bool toolsRefused(const ApiError&);
[[nodiscard]] bool streamOptionsRefused(const ApiError&);
// La liste des modeles (GET <base>/models) : id, nom, fenetre de contexte si le service la dit.
[[nodiscard]] ModelsResult parseOpenAIModels(int status, std::string_view body);

// Les octets du flux -> les ProviderEvent, sans fil ni reseau.
class OpenAIStream {
public:
    void feed(std::string_view chunk);
    void end();   // la connexion s'est terminee : sans [DONE] ni finish_reason, c'est une coupure
    [[nodiscard]] std::vector<ProviderEvent> take();
    [[nodiscard]] bool emitted() const noexcept { return emitted_; }
    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const ApiError& error() const noexcept { return error_; }
    void setModel(std::string model) { model_ = std::move(model); }
    // La proposition est attendue en texte (un bloc ```json) : lue a la fin, rendue en ToolUse.
    void setTextTool(bool on) noexcept { textTool_ = on; }

private:
    struct Call {
        std::string id, name, args;
    };
    void handle(const std::string& data);
    void finish();
    void status(const char* s);

    SseReader                  sse_;
    std::map<int, Call>        calls_;
    std::vector<ProviderEvent> out_;
    std::string                finishReason_, phase_, model_, text_;
    bool                       done_{false}, finished_{false}, failed_{false}, emitted_{false}, textTool_{false};
    ApiError                   error_;
    ProviderUsage              usage_;
};

class OpenAIProvider final : public Provider {
public:
    using Options = AnthropicProvider::Options;
    OpenAIProvider();
    explicit OpenAIProvider(Options o);
    ~OpenAIProvider() override;

    [[nodiscard]] std::string_view key() const noexcept override { return "openai"; }
    bool start(ProviderRequest request, std::string* why = nullptr) override;
    [[nodiscard]] std::vector<ProviderEvent> poll() override;
    void cancel() override;
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    void run(ProviderRequest request, OpenAITarget target);
    void push(ProviderEvent e);
    bool waitFor(double seconds);

    Options                     opt_;
    std::thread                 thread_;
    mutable std::mutex          m_;
    std::condition_variable     cv_;
    std::vector<ProviderEvent>  events_;
    std::shared_ptr<HttpClient> http_;
    std::atomic<bool>           busy_{false};
    std::atomic<bool>           cancelled_{false};
};

} // namespace app::ai
