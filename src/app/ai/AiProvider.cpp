// =============================================================================
//  app/ai/AiProvider.cpp - les fournisseurs d'une cle des parametres
// =============================================================================
#include "AiProvider.hpp"

#include "AiAnthropic.hpp"
#include "AiOpenAI.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace app::ai {

OutputCap outputCapFrom(std::string_view message, int asked) {
    OutputCap out;
    std::string m(message);
    std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&](std::string_view w) { return m.find(w) != std::string::npos; };
    const bool aboutOutput = has("max_tokens") || has("max_completion_tokens") || has("max_output_tokens") || has("maxoutputtokens")
                             || has("output tokens") || has("completion tokens") || has("in the completion") || has("num_predict");
    if (!aboutOutput || asked <= kMinOutputTokens) return out;
    // Les nombres du message, et le signe qui separe chacun du suivant ("+", ">", "<=", ...).
    struct Num {
        long long   value{0};
        std::size_t at{0}, end{0};
    };
    std::vector<Num> nums;
    for (std::size_t i = 0; i < m.size();) {
        if (std::isdigit(static_cast<unsigned char>(m[i])) && (i == 0 || !std::isalpha(static_cast<unsigned char>(m[i - 1])))) {
            std::size_t j = i;
            long long v = 0;
            while (j < m.size() && (std::isdigit(static_cast<unsigned char>(m[j])) || (m[j] == ',' && j + 3 < m.size() && std::isdigit(static_cast<unsigned char>(m[j + 1])))))
                if (m[j++] != ',' && v < 1000000000LL) v = v * 10 + (m[j - 1] - '0');
            nums.push_back({v, i, j});
            i = j;
        } else {
            ++i;
        }
    }
    const auto between = [&](std::size_t k) {   // le texte entre nums[k] et nums[k + 1], sans les blancs
        std::string s;
        for (std::size_t i = nums[k].end; i < nums[k + 1].at; ++i)
            if (!std::isspace(static_cast<unsigned char>(m[i]))) s += m[i];
        return s;
    };
    const auto take = [&](long long n, bool model) {
        if (n >= kMinOutputTokens && n < asked) {
            out.next = static_cast<int>(n);
            out.modelLimit = model;
            return true;
        }
        return false;
    };
    // 1) << entree + longueur > contexte >> : la place qui reste, pour cette demande (une marge pour le compte).
    for (std::size_t k = 0; k + 2 < nums.size(); ++k)
        if (between(k) == "+" && between(k + 1) == ">" && nums[k + 1].value == asked)
            if (take(nums[k + 2].value - nums[k].value - 512, false)) return out;
    // 2) << demande > N >> (Anthropic : la limite du modele).
    for (std::size_t k = 0; k + 1 < nums.size(); ++k)
        if (nums[k].value == asked && between(k) == ">" && take(nums[k + 1].value, true)) return out;
    // 3) << at most N >>, << <= N >>, << maximum ... N >>, << less than or equal to N >>, << up to N >>.
    static const char* const kSays[] = {"at most", "<=", "less than or equal to", "maximum value", "maximum allowed", "maximum is", "up to", "limit is"};
    for (const char* say : kSays)
        for (std::size_t p = m.find(say); p != std::string::npos; p = m.find(say, p + 1))
            for (const auto& n : nums)
                if (n.at > p && n.at - p < 48 && n.value != asked && take(n.value, true)) return out;
    // 4) un refus de la longueur sans chiffre utilisable : la moitie (pour cette demande).
    (void)take(asked / 2, false);
    return out;
}

std::unique_ptr<Provider> makeProvider(std::string_view key) {
    if (key == "anthropic") return std::make_unique<AnthropicProvider>();
    if (key == "openai") return makeOpenAIProvider();   // 1.16.0 (le contrat : nul tant que le chantier ne l'a pas rempli)
    return nullptr;   // "demo" (son propre chemin, sans reseau), ou un fournisseur a venir
}

} // namespace app::ai
