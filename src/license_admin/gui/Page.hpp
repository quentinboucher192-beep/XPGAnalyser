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
//    - une fonctionnalite qui n'existe pas n'est jamais simulee : la page le dit
//      (1.46.0 : la boutique et les commandes existent - Stripe, PageShop.cpp) ;
//    - les textes en francais (UTF-8 en \xHH dans le C++), tutoiement.
// =============================================================================
#pragma once

#include "Workspace.hpp"

#include "../ServiceDeploy.hpp"   // 1.45.0 : deployer le service, sa version
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
    // 1.44.0 : un travail lent SANS BRUIT (un releve automatique : les admins) - rien au journal de la seance, la
    // coquille n'est pas << occupee >> ; `done` dit ce qu'il faut. Vide (les essais) : runSlow.
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
//  "demandes"       1.48.1 : plus une page - un lien vers "commandes" (les demandes de licence ; Shell::navigate) ;
//                   l'ancienne page (makeRequestsPage, PageRequests.cpp) reste pour ses essais, et requestflow (ses gestes)
//  "flottantes"     Licences flottantes (le service, 1.35)   PageFloating.cpp
//  "boutique"       Boutique (1.46.0 : Stripe, le catalogue) PageShop.cpp
//  "commandes"      Commandes et demandes (1.46.0 : livrer, rembourser ; 1.48.0 : le panier ; 1.48.1 : les demandes de
//                   licence, un article rembourse seul)       PageShop.cpp
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
//  "versions"       Versions (publier ; l'admin+)            PageVersions.cpp
//  1.47.0 : "globe-essai" la page d'essai du globe (cachee : --page seulement ; des villes d'essai) PageGlobeTest.cpp
//  1.47.0 : "statistiques" Statistiques (sous Pilotage ; les courbes, planches 08 et 09) PageStats.cpp, PageStatsUpdates.cpp
//  1.47.0 : "client"      Clients > la fiche d'un client et ses courbes (une sous-page, planche 10) PageClientCard.cpp
//  1.47.0 : "globe"       Statistiques > Le globe (une sous-page ; le service) PageStatsGlobe.cpp
//  1.47.0 : "graphes-essai" la galerie des graphiques (cachee : --page seulement ; donnees d'essai) PageChartsGallery.cpp
//  1.45.0 : le dialogue << Deployer le service >> (Service en ligne, le tableau de bord) DeployDialog.cpp
//  1.47.0 : "compte"          Mon compte (tous les roles : la cle, l'etat, les pouvoirs) PageAccount.cpp
//  1.51.0 : plus de page "demandes-admins" (Demandes d'admin) : un admin s'ajoute par son code d'invitation, signe d'office
PagePtr makeDashboardPage(ShellHost host);
PagePtr makeClientsPage(ShellHost host);
PagePtr makeLicencesPage(ShellHost host);
PagePtr makeEditionsPage(ShellHost host);
PagePtr makeFeaturesPage(ShellHost host);
PagePtr makeOptionsPage(ShellHost host);   // 1.42.0
PagePtr makeActivationsPage(ShellHost host);
PagePtr makeRequestsPage(ShellHost host);
PagePtr makeFloatingPage(ShellHost host);
PagePtr makeShopPage(ShellHost host);      // 1.46.0 : "boutique"
PagePtr makeOrdersPage(ShellHost host);    // 1.46.0 : "commandes"
PagePtr makeRevocationsPage(ShellHost host);
PagePtr makeServicePage(ShellHost host);
PagePtr makeHistoryPage(ShellHost host);
PagePtr makeSecurityPage(ShellHost host);
PagePtr makeAdminsPage(ShellHost host);    // 1.44.0
PagePtr makeRightsPage(ShellHost host);    // 1.44.0
PagePtr makeJournalPage(ShellHost host);   // 1.44.0
PagePtr makeVersionsPage(ShellHost host);  // 1.44.0
PagePtr makeGlobeTestPage(ShellHost host); // 1.47.0 : cachee (--page globe-essai)
PagePtr makeGlobePage(ShellHost host);     // 1.47.0 : "globe", Statistiques > Le globe (PageStatsGlobe.cpp)
PagePtr makeStatsPage(ShellHost host);     // 1.47.0 : "statistiques" (planches 08, 09 ; PageStats.cpp)
PagePtr makeClientCardPage(ShellHost host);   // 1.47.0 : "client", Clients > un client et ses courbes (planche 10 ; PageClientCard.cpp)
PagePtr makeChartsGalleryPage(ShellHost host);   // 1.47.0 : cachee (--page graphes-essai)
PagePtr makeAccountPage(ShellHost host);         // 1.47.0 : "compte", Mon compte (Securite ; PageAccount.cpp)
// 1.52.0 : "poste", Mon compte > Ce PC - XPG Admin sans derogation : ce que ce PC regle lui-meme (l'adresse du service, la
// connexion signee, l'archive recue par le trousseau, le mail au client) ; la page Service en ligne dans son mode << poste >>
// (PageService.cpp), sans rien de ce qui gere le service (ClassicView.hpp).
PagePtr makePcPage(ShellHost host);

// 1.44.0 : CE QU'UNE PAGE DEMANDE A UNE AUTRE de montrer en arrivant (<< Voir son journal >> : le journal filtre sur cet
// admin ; << Ses droits >> : cet admin choisi dans Droits ; << Voir la licence >> du journal : la licence choisie) - la page
// le prend a son prochain refresh (la coquille l'appelle en la montrant). `what` : "admin:<cle>", "ligne:<numero>",
// l'identifiant d'une licence... ; takePageFocus le rend une fois (vide : rien).
void requestPageFocus(const std::string& pageId, const std::string& what);
[[nodiscard]] std::string takePageFocus(const std::string& pageId);

// 1.44.0 : le dialogue << Ajouter un admin >> (planche 02) ; 1.51.0 : une seule etape - son nom, son courriel, son
// profil, le PC de son Studio (une recherche) ou un lien ; l'invitation part, son code l'inscrit, l'Admin+ le signe
// d'office. `done` (facultatif) : appele quand l'invitation est partie.
menu::MenuPtr makeAddAdminDialog(ShellHost host, std::function<void()> done = {});
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

// 1.48.1 : LES GESTES D'UNE DEMANDE DE LICENCE (l'assistant << Demander ma licence >> du Studio : POST /v1/demandes), les
// memes dans << Commandes et demandes >> (PageShop.cpp : Delivrer, Refuser...) et dans l'ancienne page des demandes
// (PageRequests.cpp, plus dans le menu) : accepter (l'assistant de licence ouvert sur la demande ; `onlineLicence` : une
// licence en ligne, avec un code), ajouter l'option demandee (la fenetre << Ajouter une option >>), refuser (la raison,
// scellee pour le PC), renvoyer la reponse d'une demande acceptee ici. Chacun dit lui-meme pourquoi il ne part pas (un
// travail en cours, une demande qui n'attend plus). `done` : apres le travail lent, l'archive relue (le fil de l'interface).
namespace requestflow {
void accept(const ShellHost& host, const online::Entry& e, bool onlineLicence);
void addOption(const ShellHost& host, const online::Entry& e);
void askRefuse(const ShellHost& host, const online::Entry& e, std::function<void()> done = {});
void resend(const ShellHost& host, const online::Entry& e, std::function<void()> done = {});
// L'edition voulue (ou l'option) d'une demande, en mots : << PROFESSIONAL \xC2\xB7 490 EUR HT / an >>, << Option : ... >>.
[[nodiscard]] std::string wanted(const Workspace& w, const online::Entry& e);
} // namespace requestflow

// 1.45.0 : LA VERSION DU SERVICE EN LIGNE (l'etat lu : Verifier, ou au lancement de l'Admin) face a ce qu'il faut a cet
// Admin (deployer::kServiceRequired) et au service de son kit (deployer::installedKit) - jamais face a la version de
// l'Admin lui-meme. `outdated` : la pastille du haut (<< a mettre a jour >>), le bandeau du tableau de bord et de la page
// Service en ligne, avec << Deployer le service >>.
[[nodiscard]] deployer::VersionCheck serviceVersion(const Workspace& w);
// 1.46.0 : LA BOUTIQUE (PageShop.cpp, docs/BOUTIQUE.md) - le dialogue << Brancher Stripe... >> (la cle secrete dans un champ
// masque, jamais gardee ; le webhook cree chez Stripe, les deux secrets poses sur le Worker par le kit) ; `done` : branche.
menu::MenuPtr makeStripeDialog(ShellHost host, std::function<void()> done = {});
// La phrase << Ton service en ligne est en 1.44.0 : deploie la 1.46.0 pour la boutique. >> quand l'etat lu dit un service
// plus ancien que shop::kServiceRequired (le reste de l'Admin marche avec lui) ; vide : il sait la boutique, ou pas lu.
[[nodiscard]] std::string shopServiceNote(const Workspace& w);

// 1.45.0 : LE DIALOGUE << Deployer le service >> (DeployDialog.cpp) : il lance tout de suite le kit installe
// (deployer::deploy, par runSlow), montre sa sortie en direct (le journal qui defile, l'etape, Annuler), puis le
// resultat : en ligne -> l'etat relu (/v1/etat) : << Service en ligne : version 1.44.0 >> ; sinon la raison en francais
// et l'etape suivante (nodejs.org ; << Se connecter a Cloudflare... >> : npx wrangler login ; les dernieres lignes).
// `done` (facultatif) : a chaque fin (la page se rafraichit).
menu::MenuPtr makeDeployDialog(ShellHost host, std::function<void()> done = {});
// 1.47.0 : la derogation << Gerer le service >> (l'admin+ ; DerogationDialog.cpp) ; 1.52.0 : << tout >>, de 10 min a 72 h,
// activee par un code (montre ici apres l'envoi) ; `adminKey`
// (facultatif) : l'admin choisi d'avance ; `done` : apres Accorder, Revoquer ou Fermer.
menu::MenuPtr makeDerogationDialog(ShellHost host, std::string adminKey = {}, std::function<void()> done = {});
// 1.52.0 : XPG Admin - taper le code de la derogation recue (DerogationDialog.cpp ; la coquille l'ouvre d'elle-meme une
// fois par derogation, la pastille << Derogation recue >> et Mon compte aussi) ; `done` : a la fermeture.
menu::MenuPtr makeDerogationCodeDialog(ShellHost host, std::function<void()> done = {});
// 1.49.0 : << Repartir de zero >> (l'admin+ ; ResetDialog.cpp, docs/ADMINS.md par. 13) - ce qui est efface et garde, la
// sauvegarde faite d'abord, le mot REPARTIR ; `done` : a la fermeture. XPG Admin (l'edition classique) : nullptr.
menu::MenuPtr makeStartOverDialog(ShellHost host, std::function<void()> done = {});

} // namespace license_admin::gui
