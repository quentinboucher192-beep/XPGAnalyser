// =============================================================================
//  app/ai/AiPaths.hpp - l'Assistant IA : le dossier de ses donnees sur le PC
// -----------------------------------------------------------------------------
//  LE CONTRAT ENTRE L'ASSISTANT ET LE CENTRE DE MEMOIRE. L'Assistant (le panneau,
//  les conversations, les pieces jointes, l'apercu) ecrit dans ces dossiers ; le
//  centre "Memoire et apprentissage de l'IA" les lit, les mesure et les nettoie.
//  Tout vit sous UNE racine, dans les reglages de l'utilisateur (a cote de Mes
//  images), jamais dans un projet :
//
//      IA/
//        parametres.json         les reglages de l'Assistant (jamais la cle API en clair)
//        conversations/          un fichier JSON par conversation (ses messages, ses propositions)
//        pieces-jointes/<id>/    les fichiers joints, copies a l'identique (les originaux)
//        connaissances/          un fichier JSON par connaissance ajoutee (utilisateur, projet, regle)
//        activite.jsonl          l'activite de l'apprentissage et de la memoire, une ligne JSON par evenement
//        index/                  les index de recherche (regenerables)
//        journaux/               les journaux techniques
//        cache/                  ce qui se regenere - SEUL ce dossier est vide par "Vider le cache" :
//          requetes/             le cache temporaire des requetes
//          traitement/           les fichiers temporaires de traitement
//          images/               les images temporaires
//          documents/            les documents temporaires
//          apercus/              les donnees de previsualisation
//          telechargements/      les fichiers telecharges qui peuvent se regenerer
//
//  La racine est fixee par l'application au demarrage (setRoot), et par chaque essai
//  sur un dossier jetable. Vide : rien ne s'ecrit (l'Assistant garde tout en memoire).
//  Les chemins sont en UTF-8, comme hmi::imagelib::userFolder().
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace app::ai {

namespace detail {
inline std::string& rootRef() {
    static std::string root;
    return root;
}
inline std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return {};
    std::string out(a);
    if (out.back() != '/' && out.back() != '\\') out += '/';
    out += b;
    return out;
}
} // namespace detail

inline void setRoot(std::string root) { detail::rootRef() = std::move(root); }
[[nodiscard]] inline const std::string& root() { return detail::rootRef(); }

[[nodiscard]] inline std::string settingsFile() { return detail::join(root(), "parametres.json"); }
[[nodiscard]] inline std::string conversationsDir() { return detail::join(root(), "conversations"); }
[[nodiscard]] inline std::string attachmentsDir() { return detail::join(root(), "pieces-jointes"); }
[[nodiscard]] inline std::string knowledgeDir() { return detail::join(root(), "connaissances"); }
[[nodiscard]] inline std::string activityFile() { return detail::join(root(), "activite.jsonl"); }
[[nodiscard]] inline std::string indexDir() { return detail::join(root(), "index"); }
[[nodiscard]] inline std::string logsDir() { return detail::join(root(), "journaux"); }
[[nodiscard]] inline std::string cacheDir() { return detail::join(root(), "cache"); }

// Les six categories du cache, dans l'ordre ou le centre de memoire les montre.
struct CacheCategory {
    std::string_view folder;    // le sous-dossier de cache/
    std::string_view label;     // ce que l'utilisateur lit (UTF-8 en \xHH)
};
inline constexpr CacheCategory kCacheCategories[] = {
    {"requetes", "Cache temporaire des requ\xC3\xAA" "tes"},
    {"traitement", "Fichiers temporaires de traitement"},
    {"images", "Images temporaires"},
    {"documents", "Documents temporaires"},
    {"apercus", "Donn\xC3\xA9" "es de pr\xC3\xA9visualisation"},
    {"telechargements", "Fichiers t\xC3\xA9l\xC3\xA9" "charg\xC3\xA9s r\xC3\xA9g\xC3\xA9n\xC3\xA9rables"},
};
[[nodiscard]] inline std::string cacheDir(std::string_view category) { return detail::join(cacheDir(), category); }

} // namespace app::ai
