// =============================================================================
//  license_admin/gui/Page.hpp - 1.33.0 : UNE PAGE DE L'APPLICATION ADMIN, ET CE
//  QUE LA COQUILLE LUI DONNE (XPG-License-Manager-Admin ; jamais livre)
// -----------------------------------------------------------------------------
//  La coquille (Shell.hpp) : a gauche le menu (les 13 entrees de la maquette ;
//  1.34.0 : 14, avec << Demandes en ligne >> apres Activations ; 1.35.0 :
//  << Service en ligne >> a la place de << Synchronisation GitHub >>),
//  en haut l'etat (la cle, le service, un travail en cours), au centre la page
//  choisie, en bas le bandeau du dernier resultat. Une page est un ui::Widget ;
//  elle lit le Workspace et demande a la coquille ce qui depasse sa surface :
//  montrer un resultat, lancer un travail lent, ouvrir un dialogue, choisir un
//  fichier, aller a une autre page.
//
//  REGLES (toutes les pages) :
//    - jamais de reseau ni d'Argon2id sur le fil de l'interface : runSlow ;
//    - une operation qui ecrit attend qu'aucun travail lent ne tourne (busy) ;
//    - un secret tape (phrase, code de secours) : InputText masque, jamais dans
//      un message, un journal, le presse-papiers (sauf le code de secours et le
//      code d'activation, copies a la demande explicite de l'utilisateur), vide
//      des que l'operation est lancee ;
//    - une fonctionnalite qui n'existe pas (la boutique, les commandes : elles
//      demandent un paiement) n'est jamais simulee : la page le dit ;
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
    // 1.44.0 : un travail lent SANS BRUIT (un releve automatique : les admins, les demandes d'admin pendant que le
    // dialogue << Ajouter un admin >> attend) - rien au journal de la seance, la coquille n'est pas << occupee >> ; `done`
    // dit ce qu'il faut. Vide (les essais) : runSlow.
    std::function<bool(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done)> runQuiet;
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
//  "options"        Options (1.42.0)                         PageEditions.cpp
//  "fonctionnalites" Fonctionnalites                         PageEditions.cpp
//  "activations"    Activations                              PageActivations.cpp
//  "demandes"       Demandes en ligne (le service, 1.35)     PageRequests.cpp
//  "flottantes"     Licences flottantes (le service, 1.35)   PageFloating.cpp
//  "boutique"       Boutique (pas encore : un paiement)      PageOnline.cpp
//  "commandes"      Commandes (pas encore : un paiement)     PageOnline.cpp
//  "revocations"    Revocations                              PageRevocations.cpp
//  "service"        Service en ligne (1.35)                  PageService.cpp
//  "historique"     Historique                               PageHistory.cpp
//  "securite"       Parametres de securite (la cle)          PageSecurity.cpp
//  1.44.0 : plusieurs admins (docs/ADMINS.md, maquette 1.43.0)
//  "admins"         Administrateurs (tous ; lecture seule      PageAdmins.cpp
//                   pour un admin ; sans cle : devenir admin)
//  "droits"         Droits des admins (une sous-page           PageRights.cpp
//                   d'Administrateurs ; l'admin+)
//  "journal"        Journal des admins (l'admin+)            PageJournal.cpp
PagePtr makeDashboardPage(ShellHost host);
PagePtr makeClientsPage(ShellHost host);
PagePtr makeLicencesPage(ShellHost host);
PagePtr makeEditionsPage(ShellHost host);
PagePtr makeFeaturesPage(ShellHost host);
PagePtr makeOptionsPage(ShellHost host);   // 1.42.0
PagePtr makeActivationsPage(ShellHost host);
PagePtr makeRequestsPage(ShellHost host);
PagePtr makeFloatingPage(ShellHost host);
PagePtr makeOnlinePage(ShellHost host, const std::string& which);   // "boutique", "commandes"
PagePtr makeRevocationsPage(ShellHost host);
PagePtr makeServicePage(ShellHost host);
PagePtr makeHistoryPage(ShellHost host);
PagePtr makeSecurityPage(ShellHost host);
PagePtr makeAdminsPage(ShellHost host);    // 1.44.0
PagePtr makeRightsPage(ShellHost host);    // 1.44.0
PagePtr makeJournalPage(ShellHost host);   // 1.44.0

// 1.44.0 : CE QU'UNE PAGE DEMANDE A UNE AUTRE de montrer en arrivant (<< Voir son journal >> : le journal filtre sur cet
// admin ; << Ses droits >> : cet admin choisi dans Droits ; << Voir la licence >> du journal : la licence choisie) - la page
// le prend a son prochain refresh (la coquille l'appelle en la montrant). `what` : "admin:<cle>", "ligne:<numero>",
// l'identifiant d'une licence... ; takePageFocus le rend une fois (vide : rien).
void requestPageFocus(const std::string& pageId, const std::string& what);
[[nodiscard]] std::string takePageFocus(const std::string& pageId);

// 1.44.0 : le dialogue << Ajouter un admin >> (planche 02) ; `requestKey` non vide : ouvert sur cette demande recue, a
// l'etape 3 (<< Verifier et accepter... >>). `done` (facultatif) : appele quand un admin a ete accepte ou refuse.
menu::MenuPtr makeAddAdminDialog(ShellHost host, std::string requestKey = {}, std::function<void()> done = {});
// 1.44.0 : le dialogue << Criticite des actions >> (planche 04) ; `done` : apres Enregistrer.
menu::MenuPtr makeCriticalityDialog(ShellHost host, std::function<void()> done = {});

// L'assistant de creation d'une licence (un dialogue) ; `requests` : des demandes deja choisies (glissees) ;
// `extendLicenseId` : ajouter des PC a cette licence de l'archive (une licence en ligne : la prolonger) ; `customer` : le
// client prerempli (page Clients) ; `online` : 1.35.0 - une licence EN LIGNE (aucun PC, un code d'activation).
menu::MenuPtr makeLicenceWizard(ShellHost host, std::vector<std::string> requests, std::string extendLicenseId = {}, std::string customer = {},
                                bool online = false);

// 1.35.0 : LE CODE D'ACTIVATION d'une licence en ligne, montre UNE fois : masque, << Afficher >>, << Copier >> (le
// presse-papiers, sur ce clic seulement), et le rappel que seule son empreinte est gardee. Le dialogue efface le code en
// se fermant.
menu::MenuPtr makeCodeDialog(std::string licenseId, std::string code);
// Deposer une licence en ligne preparee (Workspace::prepareOnlineLicence, prepareNewCode) par runSlow, puis montrer son
// code (makeCodeDialog) si le depot a reussi ; un echec : le code est oublie, et c'est dit. `done` (facultatif) recoit le
// resultat. Faux : un travail tourne deja (rien n'est parti ; le code est oublie).
bool depositOnlineLicence(const ShellHost& host, ServiceJob job, std::string code, std::function<void(const OpResult&)> done = {});

// 1.35.0 : publier sur le service ce que la derniere operation a signe (Workspace::takeServiceJob) - une licence a tous
// ses PC, une revocation, un PC retire, un registre - par runSlow ; `done` (facultatif) recoit le resultat sur le fil de
// l'interface. Faux : rien a publier (le service n'est pas regle), ou un travail tourne deja (la publication attend alors
// dans le Workspace : la coquille la lance des qu'il a fini). Jamais bloquant ; un echec est dit tel quel (le fichier
// signe reste dans l'archive).
bool publishToService(const ShellHost& host, std::function<void(const OpResult&)> done = {});
// Relever les licences en ligne (GET /admin/licences) par runSlow, et adopter le releve (`done` facultatif).
bool refreshOnlineLicences(const ShellHost& host, std::function<void(const OpResult&)> done = {});

} // namespace license_admin::gui
