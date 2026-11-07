// =============================================================================
//  license_admin/gui/Page.hpp - 1.33.0 : UNE PAGE DE L'APPLICATION ADMIN, ET CE
//  QUE LA COQUILLE LUI DONNE (XPG-License-Manager-Admin ; jamais livre)
// -----------------------------------------------------------------------------
//  La coquille (Shell.hpp) : a gauche le menu (les 13 entrees de la maquette ;
//  1.34.0 : 14, avec << Demandes en ligne >> apres Activations),
//  en haut l'etat (la cle, la synchro, un travail en cours), au centre la page
//  choisie, en bas le bandeau du dernier resultat. Une page est un ui::Widget ;
//  elle lit le Workspace et demande a la coquille ce qui depasse sa surface :
//  montrer un resultat, lancer un travail lent, ouvrir un dialogue, choisir un
//  fichier, aller a une autre page.
//
//  REGLES (toutes les pages) :
//    - jamais de reseau ni d'Argon2id sur le fil de l'interface : runSlow ;
//    - une operation qui ecrit attend qu'aucun travail lent ne tourne (busy) ;
//    - un secret tape (phrase, jeton, code de secours) : InputText masque, jamais
//      dans un message, un journal, le presse-papiers (sauf le code de secours,
//      copie a la demande explicite de l'utilisateur), vide des que l'operation
//      est lancee ;
//    - une fonctionnalite qui n'existe pas (le service en ligne) n'est jamais
//      simulee : la page le dit ;
//    - les textes en francais (UTF-8 en \xHH dans le C++), tutoiement.
// =============================================================================
#pragma once

#include "Workspace.hpp"

#include "../../menu/IMenu.hpp"
#include "../../ui/Widget.hpp"

#include <functional>
#include <string>
#include <vector>

namespace license_admin::gui {

// Le ton d'un message du bandeau.
enum class Tone : int { Info = 0, Ok = 1, Warning = 2, Error = 3 };

struct ShellHost {
    std::function<Workspace&()> workspace;
    // Montrer un resultat (le bandeau, le journal de la seance) ; ok : tout est relu, chaque page rafraichie.
    std::function<void(const OpResult&)> report;
    std::function<void(const std::string& text, Tone tone)> flash;
    // Un travail lent (reseau, Argon2id) : `work` tourne sur un autre fil, `done` est appele sur celui de
    // l'interface avec son resultat (puis report). Faux : un travail tourne deja.
    std::function<bool(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done)> runSlow;
    std::function<bool()> busy;
    std::function<void(const std::string& pageId)> navigate;
    // Un dialogue au-dessus de la fenetre (l'assistant de licence, une confirmation).
    std::function<void(menu::MenuPtr)> openDialog;
    // Ouvrir ou enregistrer un fichier (l'explorateur du systeme) ; `done` recoit le chemin ("" : annule),
    // sur le fil de l'interface.
    std::function<void(const ui::FilePick& pick, std::function<void(std::string)> done)> pickFile;
    // Ouvrir un dossier dans l'explorateur (le dossier de l'archive, d'un fichier ecrit).
    std::function<void(const std::string& folder)> openFolder;
    // 1.34.0 : ouvrir une page de GitHub dans le navigateur du systeme (https://github.com/... seulement).
    std::function<void(const std::string& url)> openUrl;
};

class Page : public ui::Widget {
public:
    Page(std::string id, ShellHost host) : ui::Widget(std::move(id)), host_(std::move(host)) {}
    // Le titre en tete de page ("Licences").
    [[nodiscard]] virtual std::string title() const = 0;
    // Le Workspace a ete relu (apres une operation, ou au retour sur la page).
    virtual void refresh() = 0;
    // Des fichiers glisses sur la fenetre pendant que cette page est montree. Faux : pas pour elle (la
    // coquille propose alors de les ranger : importFiles).
    virtual bool dropFiles(const std::vector<std::string>& paths) {
        (void)paths;
        return false;
    }

protected:
    [[nodiscard]] Workspace& ws() const { return host_.workspace(); }
    ShellHost host_;
};

using PagePtr = std::unique_ptr<Page>;

// ---- les pages (une fabrique par page ; l'identifiant est celui du menu) ----
//  "tableau"        Tableau de bord                          PageDashboard.cpp
//  "clients"        Clients                                  PageClients.cpp
//  "licences"       Licences (et l'assistant de creation)    PageLicences.cpp, LicenceWizard.cpp
//  "editions"       Editions                                 PageEditions.cpp
//  "fonctionnalites" Fonctionnalites                         PageEditions.cpp
//  "activations"    Activations                              PageActivations.cpp
//  "demandes"       Demandes en ligne (la passerelle, 1.34)  PageRequests.cpp
//  "flottantes"     Licences flottantes (service en ligne)   PageOnline.cpp
//  "boutique"       Boutique (service en ligne)              PageOnline.cpp
//  "commandes"      Commandes (service en ligne)             PageOnline.cpp
//  "revocations"    Revocations                              PageRevocations.cpp
//  "github"         Synchronisation GitHub                   PageGithub.cpp
//  "historique"     Historique                               PageHistory.cpp
//  "securite"       Parametres de securite (la cle)          PageSecurity.cpp
PagePtr makeDashboardPage(ShellHost host);
PagePtr makeClientsPage(ShellHost host);
PagePtr makeLicencesPage(ShellHost host);
PagePtr makeEditionsPage(ShellHost host);
PagePtr makeFeaturesPage(ShellHost host);
PagePtr makeActivationsPage(ShellHost host);
PagePtr makeRequestsPage(ShellHost host);
PagePtr makeOnlinePage(ShellHost host, const std::string& which);   // "flottantes", "boutique", "commandes"
PagePtr makeRevocationsPage(ShellHost host);
PagePtr makeGithubPage(ShellHost host);
PagePtr makeHistoryPage(ShellHost host);
PagePtr makeSecurityPage(ShellHost host);

// L'assistant de creation d'une licence (un dialogue) ; `requests` : des demandes deja choisies (glissees) ;
// `extendLicenseId` : ajouter des PC a cette licence de l'archive ; `customer` : le client prerempli (page Clients).
menu::MenuPtr makeLicenceWizard(ShellHost host, std::vector<std::string> requests, std::string extendLicenseId = {}, std::string customer = {});

// 1.34.0 : publier sur la passerelle ce que la derniere operation a signe (Workspace::takeGatewayJob) - une licence a
// tous ses PC, une revocation, un PC retire, un registre - par runSlow ; `done` (facultatif) recoit le resultat sur le
// fil de l'interface. Faux : rien a publier (la passerelle n'est pas reglee), ou un travail tourne deja (la publication
// attend alors dans le Workspace : la coquille la lance des qu'il a fini). Jamais bloquant ; un echec est dit tel quel
// (le fichier signe reste dans l'archive).
bool publishToGateway(const ShellHost& host, std::function<void(const OpResult&)> done = {});
// L'adresse de GitHub qu'on ose ouvrir : https://github.com/<proprietaire>/<nom>[/issues/<n>] (rien d'autre).
[[nodiscard]] bool safeGithubUrl(const std::string& url);

} // namespace license_admin::gui
