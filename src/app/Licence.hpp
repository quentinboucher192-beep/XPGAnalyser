// =============================================================================
//  app/Licence.hpp - 1.30.0 : LES LICENCES DANS L'APPLICATION
// -----------------------------------------------------------------------------
//  Le module commun (src/licensing, docs/LICENCES.md) dit quelles fonctionnalites
//  sont ouvertes ; ici, ce que l'application en fait :
//
//    start()   au lancement : le dossier des licences de ce PC, le registre livre
//              avec le programme (<programme>/licences/registre.xpgreg) et, 1.32.0,
//              la liste de revocation livree (<programme>/licences/revocations.xpgrev) ;
//    tick()    une fois par heure, l'etat est revalue (une licence prend fin
//              pendant que le logiciel tourne : rien ne se ferme, seules les
//              ENTREES des fonctionnalites se verrouillent) ;
//    gate()    UN SEUL CONTROLE, aux points d'entree des fonctionnalites (la table
//              gates() : une action, un volet, un geste -> l'identifiant de sa
//              fonctionnalite). Ouverte : vrai. Fermee : la fenetre << Pas dans ta
//              licence >> (la phrase du module, et << Licence... >>), faux.
//
//    require() 1.32.0 (H3) : LE MEME CONTROLE AUX POINTS DE PASSAGE, la ou la
//              fonctionnalite se fait quel que soit le geste qui l'a demandee (la
//              simulation de l'IHM qui demarre, la validation d'un import de vues,
//              les commandes de script). Sans App : ouverte -> vrai ; fermee ->
//              faux et la phrase du module ; c'est a l'appelant de la dire.
//
//  JAMAIS DE PERTE DE TRAVAIL : Ouvrir, Enregistrer, Enregistrer sous, Exporter
//  le projet, Fermer, Quitter ne sont dans aucune entree de gates() - un essai le
//  verifie. Le poste d'exploitation (--ihm) n'est jamais arrete par la licence du
//  PC : un atelier ne s'arrete pas pour une licence.
//
//  SANS CLE D'EDITEUR dans le programme (le module : << Non activees >>), et
//  avant start() (les essais, les outils) : tout est ouvert.
// =============================================================================
#pragma once

#include "../licensing/Store.hpp"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace app {

class App;

namespace licence {

// Ou une fonctionnalite se demande.
enum class Where : unsigned char { Action, HmiPane, ApiPane, Gesture };

struct Gate {
    Where       where;
    const char* key;        // l'action ("ia.assistant"), le volet ("scripts"), le geste ("sim.run")
    const char* feature;    // l'identifiant de la fonctionnalite ("ai_assistant")
    const char* label;      // ce que voit l'utilisateur (UTF-8) : "Assistant IA"
};
// Tous les points d'entree verrouillables (l'ecran Licence les montre, les essais les verifient).
[[nodiscard]] const std::vector<Gate>& gates();
// La fonctionnalite d'un point d'entree ; vide : aucun verrou.
[[nodiscard]] std::string_view featureOf(Where where, std::string_view key);

// Au lancement (App) : configure le module. XPG_LICENCES_DOSSIER (environnement) remplace le dossier des
// licences de ce PC - 1.32.0 : SEULEMENT dans une construction d'essai (licensing::testFolderOverride, option
// CMake XPG_LICENCES_CLE_ESSAI : les captures) ; un programme livre l'ignore.
void start();
// Les essais : configurer avec ces options (un dossier, des cles de confiance).
void start(licensing::StoreOptions options);
// Chaque image : une revaluation par heure.
void tick();
// Revaluer tout de suite (apres un import).
void refresh();
// L'application a configure le module (start).
[[nodiscard]] bool active() noexcept;
// Ouverte (ou rien de configure, ou pas de cle d'editeur : tout est ouvert).
[[nodiscard]] bool allows(std::string_view feature);
// Le controle d'un point d'entree : ouverte -> vrai ; fermee -> la fenetre << Pas dans ta licence >>, faux.
bool gate(App& app, std::string_view feature);
bool gate(App& app, Where where, std::string_view key);

// 1.32.0 (H3) : le controle aux points de passage (voir l'en-tete). EN LIGNE DANS CET EN-TETE : les fichiers qui
// l'appellent (app/hmi/HmiSimulation.cpp, HmiImportDialog.cpp) se compilent aussi dans des essais sans Licence.cpp
// ni les licences (hmi_editor_test) ; start() branche le vrai controle. Avant start() : ouverte, comme allows().
namespace detail {
using RequireFn = bool (*)(std::string_view feature, std::string* why);
inline std::atomic<RequireFn> g_require{nullptr};
} // namespace detail
[[nodiscard]] inline bool require(std::string_view feature, std::string* why = nullptr) {
    const detail::RequireFn f = detail::g_require.load(std::memory_order_acquire);
    return f == nullptr || f(feature, why);
}
// La phrase d'une entree verrouillee (le module), ou vide si elle est ouverte.
[[nodiscard]] std::string lockedText(std::string_view feature);
// Le libelle d'une fonctionnalite (le registre ; sinon son identifiant).
[[nodiscard]] std::string featureLabel(std::string_view feature);

// La pastille de la barre d'etat : "Licence PROFESSIONAL \xC2\xB7 valide jusqu'au 2027-03-31".
struct Badge {
    std::string text;
    std::string tip;
    int         tone{0};     // 0 neutre, 1 bon, 2 attention, 3 erreur
};
[[nodiscard]] Badge badge();

} // namespace licence

// La fenetre Licence (le License Manager du client) ; `tab` : 0 Ma licence, 1 Mes appareils,
// 2 Fonctionnalites, 3 Editions, 4 Historique, 5 Parametres.
void showLicenceWindow(App& app, int tab = 0);

} // namespace app
