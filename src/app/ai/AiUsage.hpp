// =============================================================================
//  app/ai/AiUsage.hpp - l'Assistant IA : ce qui a ete consomme (les jetons)
// -----------------------------------------------------------------------------
//  1.16.0 (le contrat ; le chantier FOURNISSEURS le remplit). Les jetons de
//  chaque reponse, cumules par jour et par mois et par fournisseur, dans
//  IA/usage.json (jamais la demande ni la cle) : la jauge du budget que
//  l'utilisateur se fixe, et l'affichage en direct de la limite.
//
//  1.16.0 (FOURNISSEURS), EN PLUS du contrat, sans reseau ni fil :
//    - LA LIMITE DU SERVICE lue dans les en-tetes d'une reponse : Anthropic
//      (anthropic-ratelimit-requests-*, -tokens-*, -input-tokens-*,
//      -output-tokens-* : le plus contraignant ; la remise est une date
//      RFC 3339) et compatibles OpenAI (x-ratelimit-limit-requests,
//      x-ratelimit-remaining-tokens... ; la remise est une duree "6m0s",
//      "59.5ms") ; retry-after (une 429) ;
//    - les jetons de chaque conversation (le meme fichier) ;
//    - LA BANDE DU PANNEAU : ses textes (la limite, la consommation, le
//      budget, le contexte), son infobulle (le detail) et sa jauge, calcules a
//      l'instant `now` (le compte a rebours avance sans rien envoyer) ;
//    - le budget depasse : la question que l'envoi pose.
// =============================================================================
#pragma once

#include "AiHttp.hpp"
#include "AiProvider.hpp"
#include "AiStore.hpp"

#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai {

struct UsageTotals {
    long long tokensToday{0}, tokensMonth{0};
    long long requestsToday{0}, requestsMonth{0};
};

// Ajoute une reponse (son usage) au cumul du fournisseur ; ecrit IA/usage.json.
void recordUsage(std::string_view provider, const ProviderUsage& usage);
// Les cumuls du jour et du mois (vide : tous les fournisseurs).
[[nodiscard]] UsageTotals usageTotals(std::string_view provider = {});

// ---- 1.16.0 (FOURNISSEURS) : en plus du contrat ------------------------------------------------
// Le fichier des cumuls (vide : pas de racine, rien ne s'ecrit).
[[nodiscard]] std::string usageFile();
// Comme recordUsage, et les jetons de la conversation `conversation` (son identifiant).
void recordUsage(std::string_view provider, const ProviderUsage& usage, std::string_view conversation);
// Les jetons d'une conversation (0 : inconnue).
[[nodiscard]] long long conversationTokens(std::string_view conversation);

// Une duree des en-tetes OpenAI ("1s", "6m0s", "59.5ms", "1h2m3s", "12" : des secondes) ; -1 : illisible.
[[nodiscard]] double parseDuration(std::string_view text);
// Une date RFC 3339 ("2026-10-31T12:00:30Z", "...+02:00") -> dans combien de secondes (>= 0) ; -1 : illisible.
[[nodiscard]] double secondsUntil(std::string_view rfc3339, std::time_t now);
// La limite dite par les en-tetes d'une reponse (les deux familles, et retry-after).
[[nodiscard]] ProviderLimits limitsFromHeaders(const HttpHeaders& headers, std::time_t now);

// "980", "9,8 k", "38,4 k", "200 k", "1,2 M" (des jetons, a la francaise).
[[nodiscard]] std::string tokensText(long long n);
// "12 s", "6 min", "1 h 05".
[[nodiscard]] std::string waitText(double seconds);

// Ce que l'Assistant sait de la limite et de la consommation (il le tient a jour).
struct LimitState {
    ProviderLimits limits;          // la derniere lue (known() : dite)
    double         at{-1};          // la seconde (de l'application) ou elle a ete lue ; -1 : jamais
    std::time_t    clock{0};        // l'heure du PC a ce moment (l'infobulle)
    std::string    provider;        // le fournisseur qui l'a dite
    bool           answered{false}; // une reponse est arrivee (meme sans en-tete de limite)
    ProviderUsage  last;            // la derniere reponse
    long long      conversation{0}; // les jetons de la conversation ouverte
    int            contextWindow{0};// la fenetre du modele (0 : inconnue)
    long long      contextUsed{0};  // le contexte de la derniere demande (ses jetons d'entree)
};

// La bande du panneau : de une a trois lignes (chacune en deux longueurs : la longue si elle
// tient, sinon la courte), l'infobulle, la jauge du budget (ratio < 0 : pas de budget ; level :
// 0 normal, 1 a 80 %, 2 depasse).
struct UsageBand {
    std::vector<std::string> lines, shortLines;
    std::string              tooltip;
    double                   ratio{-1};
    int                      level{0};
    bool                     waiting{false};   // une 429 : le service demande d'attendre
};
// `local` : le service est sur ce PC (ou le reseau de l'usine) ; `now` : la seconde de l'application.
[[nodiscard]] UsageBand usageBand(const Settings&, const LimitState&, const UsageTotals&, bool local, double now);
// Le budget est-il atteint ? Vrai et `question` : "Ton budget du jour est atteint (210 k / 200 k
// jetons). Envoyer quand meme ?"
[[nodiscard]] bool budgetExceeded(const Settings&, const UsageTotals&, std::string* question = nullptr);

} // namespace app::ai
