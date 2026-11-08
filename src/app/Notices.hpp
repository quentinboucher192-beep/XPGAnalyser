// =============================================================================
//  app/Notices.hpp - 1.38.0 : LES NOTIFICATIONS GENERALES DU STUDIO
// -----------------------------------------------------------------------------
//  << Dans les Studios, dans les menus principaux, un systeme de notifications
//  generales - et on y envoie les notifications des licences. >>
//
//  LE CARNET : le meme journal que celui de l'Assistant (ai/AiNotifications :
//  sur entre fils, une cle remplace, lu / non lu, borne a 300, garde sur le
//  disque ecrite sur un fil a part, au plus une fois par seconde) - un second
//  carnet, a lui : <les reglages de l'application>/notifications.json (chaque
//  Studio le sien). La cloche de l'accueil et celle de la barre du projet en
//  montrent le nombre non lu ; un clic ouvre la fenetre Notifications.
//
//  CE QUI Y VA (jamais par mail : l'editeur ne spamme pas ses clients) :
//    - la licence : installee, renouvelee, acceptee ou refusee, bientot finie,
//      revoquee ;
//    - les offres : une offre dont les fonctionnalites changent (l'avant et
//      l'apres), une offre nouvelle ou retiree, une reduction ;
//    - les annonces de l'editeur (<< Prevenir mes clients >>) - un client hors
//      connexion les recoit a sa prochaine connexion (elles voyagent dans le
//      registre signe des editions).
//
//  LE BOUTON d'une notification : un lien que la fenetre sait suivre.
//    "licence:<onglet>"   la fenetre Licence sur cet onglet (licence::kTab...)
// =============================================================================
#pragma once

#include "ai/AiNotifications.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::notices {

using Item = app::ai::notifs::Item;

// ---- le carnet (sur entre fils ; le mecanisme : ai/AiNotifications.cpp) --------------------------------------------
std::uint64_t post(Item n);
[[nodiscard]] std::vector<Item> list();            // la plus recente d'abord
[[nodiscard]] std::optional<Item> find(std::uint64_t id);
[[nodiscard]] std::size_t size();
[[nodiscard]] std::size_t unread();
[[nodiscard]] bool unreadError();
bool markRead(std::uint64_t id, bool read = true);
std::size_t markAllRead();
bool remove(std::uint64_t id);
std::size_t clearRead();
[[nodiscard]] std::uint64_t revision();

// ---- la garde sur le disque ----------------------------------------------------------------------------------------
void open(const std::string& file);
void close();
[[nodiscard]] bool opened();
bool waitIdle(int timeoutMs);
[[nodiscard]] std::string lastError();
void resetForTests();

// ---- le bouton ------------------------------------------------------------------------------------------------------
[[nodiscard]] inline std::string toLicence(int tab) { return "licence:" + std::to_string(tab); }
// 1.39.0 : la fenetre du detail d'une notification (l'offre modifiee : l'avant / l'apres) - sa cle.
[[nodiscard]] inline std::string toDetail(const std::string& key) { return "detail:" + key; }
struct Target {
    std::string kind;   // "licence", "detail" (1.39.0) ; vide : inconnu
    int         tab{-1};
    std::string key;    // detail : la cle de la notification
};
[[nodiscard]] inline Target parseAction(std::string_view action) {
    Target t;
    if (action.rfind("detail:", 0) == 0 && action.size() > 7 && action.size() < 300) {
        t.kind = "detail";
        t.key = std::string(action.substr(7));
        return t;
    }
    if (action.rfind("licence:", 0) == 0) {
        t.kind = "licence";
        int n = 0;
        bool digits = action.size() > 8;
        for (std::size_t i = 8; i < action.size(); ++i) {
            const char c = action[i];
            if (c == '-' && i == 8) continue;
            if (c < '0' || c > '9') {
                digits = false;
                break;
            }
            n = n * 10 + (c - '0');
            if (n > 1000) break;
        }
        t.tab = digits ? (action.size() > 8 && action[8] == '-' ? -n : n) : -1;
    }
    return t;
}

} // namespace app::notices
