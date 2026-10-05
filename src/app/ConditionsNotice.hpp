#pragma once
// =============================================================================
//  app/ConditionsNotice.hpp - 1.11 (R111, decisions 6 et 15 du 03/10) : dire
//  les conditions d'activation manquantes d'un projet importe avant la 1.8.0
// -----------------------------------------------------------------------------
//  Le signal est project::missingTaskConditions (ActivationConditions). Ici,
//  ce que l'appli en fait :
//    - un avis de la cloche, pose a l'ouverture du projet (le bandeau haut le
//      tient a jour chaque seconde : un reimport du .XPG qui rend les
//      conditions le retire, Ctrl+Z le remet) ;
//    - « Verifier l'ordre » dit le meme message (celui de l'avis pose) ;
//    - le bouton de l'avis, « Ne plus le dire pour ce projet », regle le faux
//      signal (un .XPG qui n'a vraiment aucune condition) : le dossier du
//      projet va dans les reglages de l'utilisateur, HORS du projet - le
//      format du projet ne change pas.
// =============================================================================
#include <string>

namespace domain { class Project; }
namespace project { struct Manifest; }

namespace app {

class Settings;

namespace conditions {

inline constexpr const char* kNoticeKey = "conditions:activation";      // la cle de l'avis de la cloche
inline constexpr const char* kSilenceAction = "conditions.taire";       // l'action de son bouton
inline constexpr const char* kSilencedSetting = "conditions.taire.projets"; // reglages : les dossiers tus
inline constexpr const char* kSilenceButton = "Ne plus le dire pour ce projet";

// Le dossier tel que les reglages le retiennent (absolu, '/' ; "" : aucun).
[[nodiscard]] std::string folderKey(const std::string& folder);

// Le message pour ce projet : "" s'il n'y a rien a dire, ou si l'utilisateur
// l'a fait taire pour ce dossier. p nul (aucun projet) : "".
[[nodiscard]] std::string message(const domain::Project* p, const project::Manifest& m,
                                  const Settings& s, const std::string& folder);

// L'avis de la cloche suit le message : pose s'il manque ou a change, retire
// ("" ) s'il y est. Rien ne change : rien (la cloche ne se rafraichit pas).
// true : l'avis a change (la ligne d'aide de « Verifier l'ordre » est a refaire).
bool syncNotice(const std::string& message);

// Le message de l'avis pose ("" : aucun) - ce que dit aussi « Verifier l'ordre ».
[[nodiscard]] std::string noticeText();
// Le meme, court, avec le remede, pour une ligne d'aide etroite ("" : aucun avis).
[[nodiscard]] std::string shortText();

// « Ne plus le dire pour ce projet » : le dossier dans les reglages (ecrits
// tout de suite s'ils ont un fichier), l'avis retire. false : aucun dossier.
bool silence(Settings& s, const std::string& folder);

} // namespace conditions
} // namespace app
