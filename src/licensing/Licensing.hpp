// =============================================================================
//  licensing/Licensing.hpp - 1.30.0 (prepare en 1.29.0) : CE QUE DEMANDENT LES
//  LOGICIELS (l'API de la maquette, onglet << Dans API Studio / IHM Studio >>)
// -----------------------------------------------------------------------------
//      if (!licensing::isFeatureEnabled("ai_generation"))
//          showLocked("ai_generation");
//      licensing::getEdition();        // "PROFESSIONAL"
//      licensing::getLicenseStatus();  // LicenseStatus::Valid, Grace...
//      licensing::getMaxDevices();     // 5
//      licensing::getExpirationDate(); // "2027-03-31" ("" : pas de fin)
//      licensing::getLicenseId();      // "XPG-2026-00042" ("" : sans licence)
//
//  AUCUN DROIT N'EST ECRIT DANS LE CODE : une entree de menu porte l'identifiant
//  de sa fonctionnalite ; la licence signee (ou, sans licence, l'edition sans
//  licence du registre signe : FREE) dit lesquelles sont ouvertes.
//
//  JAMAIS DE PERTE DE TRAVAIL (la regle, pour le branchement des menus) :
//    - OUVRIR, ENREGISTRER, ENREGISTRER SOUS, EXPORTER, FERMER, QUITTER ne
//      demandent JAMAIS isFeatureEnabled : aucun identifiant de fonctionnalite
//      sur ces entrees, quel que soit l'etat (Expiree, Revoquee, Invalide, Autre
//      PC, Absente, Horloge reculee) ;
//    - une licence qui prend fin pendant le travail ne ferme rien : le projet
//      ouvert reste ouvert ; seules les ENTREES de fonctionnalites se ferment
//      (au prochain refresh()), avec la phrase de getStatusMessage() ;
//    - une fonctionnalite fermee en cours d'usage (une generation IA lancee...)
//      va a son terme ; c'est la suivante qui est refusee.
//
//  LE CYCLE : initialize() au demarrage (le dossier des licences, le registre
//  livre avec le programme) ; refresh() apres un import, et de temps en temps
//  (une fois par heure : une licence prend fin pendant que le logiciel tourne) ;
//  les lectures (isFeatureEnabled...) ne lisent aucun fichier : elles lisent le
//  dernier etat, sous un verrou (on peut les appeler de n'importe quel fil).
//  Avant initialize() : Absente, aucune fonctionnalite ouverte.
// =============================================================================
#pragma once

#include "Store.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace licensing {

// Configure le module et fait une premiere evaluation. Rend l'etat.
LicenseState initialize(StoreOptions options);
// Relit les fichiers et revalue (l'heure a avance, un import a eu lieu). Rend l'etat.
LicenseState refresh();

// ---- l'API de la maquette ----
[[nodiscard]] bool isFeatureEnabled(std::string_view featureId);
[[nodiscard]] std::string getEdition();
[[nodiscard]] LicenseStatus getLicenseStatus();
[[nodiscard]] int getMaxDevices();
[[nodiscard]] std::string getExpirationDate();
[[nodiscard]] std::string getLicenseId();

// ---- de quoi afficher ----
// La phrase claire de l'etat ("Licence PROFESSIONAL valide jusqu'au 2027-03-31.").
[[nodiscard]] std::string getStatusMessage();
// "Valide", "Gr\xC3\xA2" "ce"... (= statusLabel(getLicenseStatus())).
[[nodiscard]] std::string getStatusLabel();
// La limite d'une fonctionnalite ouverte ("max_views") ; rien : pas de limite, ou fonctionnalite fermee.
[[nodiscard]] std::optional<std::int64_t> getFeatureLimit(std::string_view featureId, std::string_view limit);
// Tout l'etat (l'ecran Licence du License Manager).
[[nodiscard]] LicenseState getLicenseState();
// La phrase d'une entree verrouillee : "Cette fonctionnalit\xC3\xA9 (ai_generation) n'est pas incluse
// dans ta licence PROFESSIONAL." + ce que dit l'etat ; le projet reste ouvert.
[[nodiscard]] std::string lockedMessage(std::string_view featureId);

// ---- l'activation par fichier (le License Manager) ----
// Importer le fichier recu de l'editeur ; l'etat est revalue.
[[nodiscard]] ImportReport importLicenseFile(const std::string& path);
// Ecrire la demande d'activation de ce PC (voir LicenseStore::writeActivationRequest).
bool writeActivationRequest(const std::string& path, const std::string& deviceName, const std::string& licenseId,
                            const std::string& software, std::string* why);

// Pour les essais : oublier la configuration (retour a << avant initialize() >>).
void resetForTests();

} // namespace licensing
