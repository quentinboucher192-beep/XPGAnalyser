// =============================================================================
//  app/ai/AiProvider.hpp - l'Assistant IA : le fournisseur (le contrat commun)
// -----------------------------------------------------------------------------
//  LE CONTRAT ENTRE LE RESEAU ET LE REGISTRE (1.15.0, phases 3 a 9). Un
//  fournisseur recoit une requete deja preparee (les consignes, l'outil des
//  operations, les tours de la conversation) et rend des evenements, morceau
//  par morceau. Il ne connait ni le projet ni les vues : la preparation est dans
//  AiPrompt.hpp (le registre des genres, le contexte), l'application des
//  operations dans AiOperations.hpp.
//
//    - start()  part sur un FIL DE TRAVAIL et rend la main tout de suite ;
//    - poll()   est appele a chaque image sur le fil de l'interface : il rend ce
//               qui est arrive depuis la derniere fois (jamais bloquant) ;
//    - cancel() est Arreter : le fil ferme la connexion, un Done "arrete" suit.
//
//  Le modele n'est JAMAIS touche par un fournisseur : un appel d'outil arrive
//  comme un evenement ToolUse (son entree JSON brute) ; l'Assistant en fait une
//  proposition (AiPrompt.hpp : proposalFromTool), la valide et la montre.
//
//  La cle API est passee dans la requete, en memoire seulement : un fournisseur
//  ne l'ecrit jamais, ni sur le disque ni dans un journal.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

// L'outil unique que le modele appelle pour proposer des operations sur la vue.
inline constexpr std::string_view kOperationsTool = "proposer_operations";

// Une piece jointe d'un tour de l'utilisateur, telle que le fournisseur l'envoie.
struct ProviderAttachment {
    std::string    name;        // "plan.pdf"
    std::string    mediaType;   // "image/png", "application/pdf", "text/csv"...
    std::string    path;        // la copie dans IA/pieces-jointes/<id>/ (UTF-8)
    std::uintmax_t bytes{0};
};

// Un tour de la conversation, dans l'ordre.
struct ProviderTurn {
    enum class Role : std::uint8_t { User, Assistant };
    Role        role{Role::User};
    std::string text;                                  // le texte (markdown simple)
    std::vector<ProviderAttachment> attachments;       // User : les pieces de ce message
    // Assistant : l'appel d'outil fait par le modele dans ce tour (vide : aucun).
    std::string toolUseId, toolName, toolInputJson;
    // User : la reponse a l'appel du tour precedent ; elle part EN TETE du tour
    // (l'API l'exige quand le tour precedent a appele un outil).
    std::string toolResultFor;                         // l'id de l'appel ; vide : aucune
    std::string toolResultText;                        // "appliquee", "refusee", "en attente"...
    bool        toolResultError{false};
};

struct ProviderRequest {
    std::string model;            // la case des parametres ; vide : start() refuse et le dit
    std::string endpoint;         // vide : l'adresse du fournisseur ; sinon un relais d'entreprise
    std::string apiKey;           // en memoire seulement
    int         timeoutSeconds{60};
    int         maxTokens{16000};
    std::string effort;           // "low" | "medium" | "high" ; vide : celui du modele
    std::string system;           // les consignes et le contexte (AiPrompt.hpp)
    std::string toolsJson;        // le tableau "tools" en JSON ; vide : aucun outil
    std::vector<ProviderTurn> turns;
};

// Ce que poll() rend. Les textes destines a l'utilisateur sont en francais.
struct ProviderEvent {
    enum class Type : std::uint8_t {
        Status,    // text : "connexion", "reflexion", "ecriture" (l'indicateur du panneau)
        Text,      // text : un morceau de la reponse
        ToolUse,   // un appel d'outil COMPLET : toolUseId, toolName, toolInputJson
        Done,      // la fin : stopReason ("end_turn", "tool_use", "max_tokens", "refusal", "arrete")
        Error      // text : ce qui ne va pas, pour l'utilisateur ; httpStatus, retryable
    };
    Type        type{Type::Status};
    std::string text;
    std::string toolUseId, toolName, toolInputJson;
    std::string stopReason;
    int         httpStatus{0};
    bool        retryable{false};
};

class Provider {
public:
    virtual ~Provider() = default;
    [[nodiscard]] virtual std::string_view key() const noexcept = 0;   // "anthropic"
    // Faux et `why` (en francais) : rien n'est parti (pas de modele, pas de cle, deja occupe...).
    virtual bool start(ProviderRequest request, std::string* why = nullptr) = 0;
    [[nodiscard]] virtual std::vector<ProviderEvent> poll() = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool busy() const noexcept = 0;
};

// Le fournisseur d'une cle des parametres (Settings::provider) ; nul : inconnu ou "demo"
// (la demonstration garde son propre chemin, AiDemo.hpp, sans reseau).
[[nodiscard]] std::unique_ptr<Provider> makeProvider(std::string_view key);

} // namespace app::ai
