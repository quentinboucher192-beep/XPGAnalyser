// =============================================================================
//  license_admin/gui/Derogation.hpp - 1.47.0 : LA DEROGATION << Gerer le
//  service >> vue par l'Admin (docs/ADMINS.md, par. 11 ; le service :
//  service/src/derogations.js)
// -----------------------------------------------------------------------------
//  Gerer le service est a l'admin+ seul. Il peut le donner a UN admin pour un
//  temps court (1 h, 4 h, 24 h ; 72 h au plus, jamais << permanente >>) : un
//  document signe par la cle de l'editeur, depose sur le service, qui en est
//  l'arbitre a SON heure. L'Admin le relit (GET /admin/etat toutes les 15 s,
//  GET /admin/derogation) et compte a rebours ; a la fin, tout redevient en
//  lecture seule de lui-meme.
//
//  L'API PUBLIQUE (Mon compte, la coquille, les pages) :
//      const Derogation& d = ws().derogation();
//      d.activeAt(ws().now())              en cours (a l'heure du service estimee) ?
//      d.remaining(ws().now())             les secondes qui restent (0 : finie)
//      derogationText(d, ws().now())       << Derogation << Gerer le service >> : il reste 52 min (jusqu'a 15:42) >>
//      remainingText(secondes)             << 52 min >>, << 3 h 05 >>
//      ws().mayManageService()             l'admin+, ou un admin dont la derogation court
//      ws().whyNotService()                vide, ou << L'admin+ seul gere le service (derogation possible). >>
// =============================================================================
#pragma once

#include "../../licensing/Time.hpp"

#include <string>

namespace license_admin::gui {

using licensing::Seconds;

// Les durees proposees (heures) ; le service refuse moins de 10 min et plus de 72 h.
inline constexpr int kDerogationHours[] = {1, 4, 24, 72};
inline constexpr Seconds kDerogationMaxSeconds = 72 * 3600;

struct Derogation {
    bool        known{false};          // lue du service (cette seance)
    std::string id;                    // 32 hexadecimaux ; vide : aucune
    std::string adminKey, adminName;   // l'admin qui la recoit
    std::string by;                    // la cle de l'editeur qui l'a signee (64 hexadecimaux)
    Seconds     start{0}, end{0}, revoked{0};
    std::string state;                 // "active", "expiree", "revoquee", "a_venir" (le service, a la lecture) ; vide : aucune
    int         uses{0};               // les gestes faits avec elle (le service les compte)
    bool        signatureOk{false};    // son document est signe par une cle de l'editeur que ce poste croit
    Seconds     serverNow{0};          // l'heure du service a la lecture
    Seconds     readAt{0};             // l'heure de ce poste a la lecture

    [[nodiscard]] bool exists() const noexcept { return !id.empty(); }
    // L'heure du service estimee maintenant (l'ecart vu a la lecture).
    [[nodiscard]] Seconds serviceNow(Seconds localNow) const noexcept { return serverNow > 0 ? serverNow + (localNow - readAt) : localNow; }
    // En cours a l'heure du service estimee (ni revoquee, ni finie).
    [[nodiscard]] bool activeAt(Seconds localNow) const noexcept {
        if (!exists() || revoked || state == "revoquee") return false;
        const Seconds t = serviceNow(localNow);
        return t >= start && t < end;
    }
    [[nodiscard]] Seconds remaining(Seconds localNow) const noexcept { return activeAt(localNow) ? end - serviceNow(localNow) : 0; }
    // Pour cette cle d'admin ?
    [[nodiscard]] bool forKey(const std::string& keyHex) const noexcept { return exists() && !keyHex.empty() && adminKey == keyHex; }
};

// << 52 min >>, << 3 h 05 >>, << 45 s >>.
[[nodiscard]] std::string remainingText(Seconds seconds);
// La phrase : << il reste 52 min (jusqu'a 15:42) >>, << revoquee le 10/11/2026 14:05 >>, << finie le ... >>, << aucune >>.
[[nodiscard]] std::string derogationText(const Derogation& d, Seconds localNow);

} // namespace license_admin::gui
