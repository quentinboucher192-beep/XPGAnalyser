// =============================================================================
//  license_admin/gui/Derogation.hpp - 1.47.0 : LA DEROGATION << Gerer le
//  service >> vue par l'Admin ; 1.52.0 : LA DEROGATION << tout >>, ACTIVEE PAR UN
//  CODE (docs/ADMINS.md, par. 11 ; le service : service/src/derogations.js)
// -----------------------------------------------------------------------------
//  Gerer le service est a l'admin+ seul. Il peut le donner a UN admin pour un
//  temps court (72 h au plus, jamais << permanente >>) : un document signe par la
//  cle de l'editeur, depose sur le service, qui en est l'arbitre a SON heure.
//  L'Admin le relit (GET /admin/etat toutes les 15 s, GET /admin/derogation) et
//  compte a rebours ; a la fin, tout redevient en lecture seule de lui-meme.
//  1.52.0 (format 2) : le droit << tout >> (l'admin voit tout comme l'admin+), une
//  duree choisie par l'admin+ (10 min a 72 h) ; ELLE COMMENCE QUAND L'ADMIN TAPE
//  LE CODE que l'admin+ voit sur son PC (8 chiffres, << 4821 7730 >>), dans l'heure
//  qui suit l'envoi ; 5 codes faux l'annulent. Le code n'est jamais garde : l'Admin+
//  le retire de sa cle (derogationCode) ; le service n'en a que l'empreinte.
//
//  L'API PUBLIQUE (Mon compte, la coquille, les pages) :
//      const Derogation& d = ws().derogation();
//      d.activeAt(ws().now())              en cours (a l'heure du service estimee) ? jamais en attente du code
//      d.pendingAt(ws().now())             1.52.0 : en attente du code (et l'heure limite pas passee) ?
//      d.remaining(ws().now())             les secondes qui restent (0 : finie)
//      derogationText(d, ws().now())       << il reste 52 min (jusqu'a 15:42) >>, << en attente du code (... 15:42) >>
//      remainingText(secondes)             << 52 min >>, << 3 h 05 >>
//      ws().mayManageService()             l'admin+, ou un admin dont la derogation court
//      ws().whyNotService()                vide, ou << L'admin+ seul gere le service (derogation possible). >>
//      ws().derogationCode(id)             1.52.0 : l'admin+ - le code (8 chiffres), retire de sa cle ; vide sinon
// =============================================================================
#pragma once

#include "../../licensing/Time.hpp"

#include <string>

namespace licensing {
class SigningKey;
}

namespace license_admin::gui {

using licensing::Seconds;

// Les durees proposees avant la 1.52.0 (heures) ; le service refuse moins de 10 min et plus de 72 h.
inline constexpr int kDerogationHours[] = {1, 4, 24, 72};
inline constexpr Seconds kDerogationMinSeconds = 10 * 60;
inline constexpr Seconds kDerogationMaxSeconds = 72 * 3600;
// 1.52.0 : les durees proposees (minutes) : 30 min, 1 h, 4 h, 24 h, 72 h ; << Autre duree >> : 10 min a 72 h.
inline constexpr int kDerogationMinutes[] = {30, 60, 240, 1440, 4320};
// 1.52.0 : le code se tape dans l'heure qui suit l'envoi ; 5 essais.
inline constexpr Seconds kDerogationCodeSeconds = 3600;
inline constexpr int kDerogationCodeTries = 5;

struct Derogation {
    bool        known{false};          // lue du service (cette seance)
    std::string id;                    // 32 hexadecimaux ; vide : aucune
    std::string adminKey, adminName;   // l'admin qui la recoit
    std::string by;                    // la cle de l'editeur qui l'a signee (64 hexadecimaux)
    Seconds     start{0}, end{0}, revoked{0};
    // "active", "expiree", "revoquee", "a_venir" ; 1.52.0 : "en_attente" (le code pas encore tape), "annulee" (le
    // service, a la lecture) ; vide : aucune
    std::string state;
    int         uses{0};               // les gestes faits avec elle (le service les compte)
    bool        signatureOk{false};    // son document est signe par une cle de l'editeur que ce poste croit
    Seconds     serverNow{0};          // l'heure du service a la lecture
    Seconds     readAt{0};             // l'heure de ce poste a la lecture
    // 1.52.0 : le format 2 (<< tout >>, active par un code)
    int         format{1};             // 1 : << Gerer le service >>, tout de suite ; 2 : << tout >>, par le code
    std::string right;                 // "service" (format 1), "tout" (format 2)
    Seconds     duration{0};           // la duree choisie par l'admin+ (secondes ; format 2)
    Seconds     validUntil{0};         // l'heure limite pour taper le code (heure du service)
    Seconds     cancelled{0};          // annulee (5 codes faux, ou le code pas tape a temps) : quand
    int         triesLeft{0};          // les essais qui restent (5 au depart)
    std::string reason;                // annulee : "code_faux", "code_perime"

    [[nodiscard]] bool exists() const noexcept { return !id.empty(); }
    // L'heure du service estimee maintenant (l'ecart vu a la lecture).
    [[nodiscard]] Seconds serviceNow(Seconds localNow) const noexcept { return serverNow > 0 ? serverNow + (localNow - readAt) : localNow; }
    // En cours a l'heure du service estimee (ni revoquee, ni finie ; 1.52.0 : ni en attente du code, ni annulee).
    [[nodiscard]] bool activeAt(Seconds localNow) const noexcept {
        if (!exists() || revoked || state == "revoquee" || state == "en_attente" || state == "annulee" || cancelled) return false;
        const Seconds t = serviceNow(localNow);
        return t >= start && t < end;
    }
    // 1.52.0 : en attente du code (le service l'a dite ainsi) et l'heure limite pas encore passee.
    [[nodiscard]] bool pendingAt(Seconds localNow) const noexcept {
        if (!exists() || revoked || cancelled || state != "en_attente") return false;
        return validUntil <= 0 || serviceNow(localNow) < validUntil;
    }
    [[nodiscard]] Seconds remaining(Seconds localNow) const noexcept { return activeAt(localNow) ? end - serviceNow(localNow) : 0; }
    // Pour cette cle d'admin ?
    [[nodiscard]] bool forKey(const std::string& keyHex) const noexcept { return exists() && !keyHex.empty() && adminKey == keyHex; }
    // 1.52.0 : le droit << tout >> (l'admin voit tout comme l'admin+), pas seulement << Gerer le service >>.
    [[nodiscard]] bool everything() const noexcept { return format >= 2 || right == "tout"; }
    // 1.52.0 : une heure du service en heure de ce PC (l'ecart vu a la lecture).
    [[nodiscard]] Seconds localTime(Seconds serviceTime, Seconds localNow) const noexcept { return serviceTime - (serviceNow(localNow) - localNow); }
};

// << 52 min >>, << 3 h 05 >>, << 45 s >>.
[[nodiscard]] std::string remainingText(Seconds seconds);
// 1.52.0 : une duree choisie : << 30 min >>, << 4 h >>, << 2 h 30 >>, << 72 h >>.
[[nodiscard]] std::string durationText(Seconds seconds);
// La phrase : << il reste 52 min (jusqu'a 15:42) >>, << revoquee le 10/11/2026 14:05 >>, << finie le ... >>, << aucune >> ;
// 1.52.0 : << en attente du code (valable jusqu'a 15:42) >>, << annulee (5 codes faux) >>.
[[nodiscard]] std::string derogationText(const Derogation& d, Seconds localNow);
// 1.52.0 : << 15:42 >> (aujourd'hui) ou << 12/11/2026 15:42 >>.
[[nodiscard]] std::string clockText(Seconds localTime, Seconds localNow);

// ---- 1.52.0 : le code (docs/ADMINS.md, par. 11) ----
// Le code de la derogation `id`, retire de la cle de l'editeur (jamais garde) : la signature Ed25519 (deterministe) de
// "XPG-DEROGATION-CODE-1\n" + id, son SHA-256, les 8 premiers octets (un entier, poids fort d'abord) modulo 10^8,
// sur 8 chiffres (<< 04821773 >>).
[[nodiscard]] std::string derogationCode(const licensing::SigningKey& editorKey, const std::string& id);
// Ce que l'Admin+ envoie a cote du document : sha256hex("XPG-DEROGATION-CODE-1\n" + id + "\n" + 8 chiffres).
[[nodiscard]] std::string derogationCodeHash(const std::string& id, const std::string& code);
// << 4821 7730 >> (pour le lire de vive voix).
[[nodiscard]] std::string formatDerogationCode(const std::string& code);
// Ce que l'admin tape (des espaces, des tirets, des points acceptes) -> 8 chiffres ; vide : pas un code.
[[nodiscard]] std::string normalizeDerogationCode(const std::string& typed);

} // namespace license_admin::gui
