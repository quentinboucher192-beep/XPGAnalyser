// =============================================================================
//  license_admin/gui/ClassicView.hpp - 1.52.0 : CE QUE VOIT UN ADMIN DANS XPG ADMIN
//  (l'edition classique ; docs/ADMINS.md, par. 1 et 12)
// -----------------------------------------------------------------------------
//  XPG Admin sans derogation : LA GESTION SEULEMENT - ni << Parametres de
//  securite >>, ni << Service en ligne >>, ni << Administrateurs >> (ni le journal
//  des admins, les droits, les versions, deja jamais). Ce dont un admin a besoin
//  pour lui-meme est dans Mon compte (son code d'invitation, sa cle, son ajout)
//  et sa sous-page << Ce PC >> (l'adresse du service, l'archive, le mail au
//  client : PageService.cpp, le mode << poste >>).
//  Une derogation de l'admin+ qui court pour CE poste (activee : Derogation::
//  activeAt) : le menu devient celui de l'admin+, tout entier ; dans les pages,
//  ce que le service permet marche, et les gestes signes par la cle de l'editeur
//  restent gris avec leur raison (kEditorKeyOnly). Elle finit : retour a la
//  gestion (Shell::Update, Mon compte).
//  L'Admin+ (l'edition Plus) ne change pas : rien ici ne le concerne.
// =============================================================================
#pragma once

#include "Workspace.hpp"

#include "../Edition.hpp"

#include <string>

namespace license_admin::gui::classic {

// La raison d'un geste signe par la cle de l'editeur, dite sous derogation (le bouton gris, son infobulle).
inline constexpr const char* kEditorKeyOnly =
    "Sign\xC3\xA9 par la cl\xC3\xA9 de l'\xC3\xA9" "diteur : l'admin+ seul, m\xC3\xAAme sous d\xC3\xA9rogation.";

// Une derogation de l'admin+ pour CE poste (sa cle d'admin), activee et en cours a l'heure du service estimee.
[[nodiscard]] inline bool derogationOpen(const Workspace& w) {
    if (w.role() != Role::Admin) return false;
    const Derogation& d = w.derogation();
    return d.forKey(w.adminKey().keyHex) && d.activeAt(w.now());
}

// XPG Admin sans derogation : le menu de la gestion seulement.
[[nodiscard]] inline bool managementOnly(const Workspace& w) { return license_admin::classique() && !derogationOpen(w); }

// Les pages que XPG Admin n'ouvre qu'avec une derogation (les trois de la demande, et les trois de l'admin+).
[[nodiscard]] inline bool needsDerogation(const std::string& pageId) {
    return pageId == "securite" || pageId == "service" || pageId == "admins" || pageId == "journal" || pageId == "droits" || pageId == "versions";
}

// Ou un admin trouve ce qui est a lui (son code d'invitation, sa cle, son ajout) : Mon compte dans XPG Admin,
// Administrateurs dans l'Admin+ (inchange).
[[nodiscard]] inline std::string accountPage() { return license_admin::classique() ? std::string("compte") : std::string("admins"); }
[[nodiscard]] inline const char* accountName() { return license_admin::classique() ? "Mon compte" : "Administrateurs"; }

// Ou regler l'adresse du service, recevoir l'archive, le mail au client : << Ce PC >> (Mon compte) dans XPG Admin sans
// derogation, Service en ligne sinon.
[[nodiscard]] inline std::string servicePage(const Workspace& w) { return managementOnly(w) ? std::string("poste") : std::string("service"); }
[[nodiscard]] inline std::string serviceName(const Workspace& w) {
    return managementOnly(w) ? std::string("Mon compte \xE2\x80\xBA Ce PC") : std::string("Service en ligne");
}

} // namespace license_admin::gui::classic
