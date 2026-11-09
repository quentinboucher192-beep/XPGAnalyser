// =============================================================================
//  license_admin/ServiceDeploy.hpp - 1.45.0 : METTRE LE SERVICE EN LIGNE DEPUIS
//  L'ADMIN (le bouton << Deployer le service >>, la commande service-deployer)
// -----------------------------------------------------------------------------
//  L'editeur oubliait de redeployer son service apres une mise a jour (le menu
//  Demarrer > Deployer le service en ligne, deployer.bat) : ses sessions par
//  Internet echouaient. Ici, l'Admin lance lui-meme le KIT installe avec lui
//  (%LOCALAPPDATA%\XPG\ServiceLicences, ou service-licences a cote de l'Admin ;
//  outils/service/LISEZ-MOI-SERVICE.txt) :
//    - Node.js est cherche comme deployer.bat le cherche (where node : le dossier
//      du kit, puis le PATH ; sous Windows, le PATH relu dans le registre en plus :
//      Node.js installe apres le lancement de l'Admin est trouve), npx aussi ;
//    - `node deployer.mjs` tourne dans le dossier du kit, SANS fenetre de console
//      (CreateProcessW, CREATE_NO_WINDOW, un tube pour la sortie et l'erreur, un
//      Job Object qui emporte ses enfants si on annule ; ailleurs fork/exec dans
//      son propre groupe), son entree fermee (jamais de question qui attend) ;
//    - chaque ligne de sa sortie passe par scrub() (un jeton, un mot de passe, un
//      code d'une adresse OAuth : masques) avant d'etre montree ou gardee ;
//    - la fin est classee (Outcome) et dite en francais, avec l'etape suivante :
//      Node.js absent (nodejs.org), pas connecte a Cloudflare (login() : `npx
//      wrangler login`, qui ouvre le navigateur), pas de sous-domaine, ou les
//      dernieres lignes.
//  Un seul deploiement a la fois dans le processus (running()). BLOQUANT : sur un
//  fil a part (l'application : runSlow), jamais sur celui de l'interface.
//
//  LA VERSION DU SERVICE (checkVersion) : celle que dit /v1/etat face a
//  kServiceRequired (ce qu'il faut a cet Admin) et a celle du kit installe (le
//  service qu'il porte) - jamais face a la version de l'Admin lui-meme : le
//  service ne change pas a chaque version.
// =============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace license_admin::deployer {

// 1.45.0 : LA VERSION DU SERVICE DONT CET ADMIN A BESOIN (au moins). A RELEVER (ici seulement) quand une version de
// l'Admin parle a une route ou lit un champ qu'un service plus ancien n'a pas ; la 1.45.0 n'a rien change au service
// (service/src/licences.js : VERSION reste 1.44.0).
inline constexpr const char* kServiceRequired = "1.44.0";
// Ce que le service de cette version-la apporte (la phrase du bandeau : << deploie la 1.44.0 pour ... >>) ; a relever avec lui.
inline constexpr const char* kServiceRequiredFor = "les sessions par Internet, les admins et leurs droits, les fichiers et les mises \xC3\xA0 jour pouss\xC3\xA9" "es";

// ---------------------------------------------------------------------------------------------------------- le kit
struct Kit {
    std::string folder;        // le dossier du kit (deployer.mjs, deployer.bat, cle-publique.txt)
    std::string script;        // deployer.mjs
    std::string service;       // le dossier du service (wrangler.toml) ; vide : introuvable
    std::string version;       // la version du service qu'il porte (src/licences.js : VERSION) ; vide : illisible
    bool        publicKey{false};   // cle-publique.txt est la (dans le kit ou dans le service)
};

// Les dossiers ou le chercher, dans l'ordre : `given` seul s'il est donne ; sinon %LOCALAPPDATA%\XPG\ServiceLicences
// (Windows), puis service-licences a cote du programme.
[[nodiscard]] std::vector<std::string> kitCandidates(const std::string& given);
// Le kit (le premier dossier qui a deployer.mjs). Faux : `why` (en francais).
bool findKit(const std::string& given, Kit& out, std::string* why = nullptr);
// Le kit trouve sans dossier donne, garde 30 s (la barre du haut le demande a chaque image) ; nul : pas de kit.
// `refresh` : le chercher de nouveau tout de suite (apres un deploiement).
[[nodiscard]] const Kit* installedKit(bool refresh = false);
// La version du service d'un dossier (src/licences.js, const VERSION = "X.Y.Z") ; vide : illisible.
[[nodiscard]] std::string serviceVersionIn(const std::string& serviceFolder);

// ---------------------------------------------------------------------------------------------------------- Node.js
struct NodeTools {
    std::string node;   // le programme node (node.exe)
    std::string npx;    // npx (npx.cmd sous Windows) ; vide : absent
    std::string path;   // le PATH donne a l'enfant
};
// Le PATH ou chercher : celui du processus et, sous Windows, celui du registre (machine puis compte) en plus.
[[nodiscard]] std::string searchPath();
// Chercher node et npx comme deployer.bat (where : le dossier `firstDir` d'abord, puis le PATH `path`). Faux : node absent.
bool findNode(const std::string& path, const std::string& firstDir, NodeTools& out);

// ---------------------------------------------------------------------------------------------------------- lancer
enum class Outcome : std::uint8_t {
    Ok,            // en ligne (ou l'essai a blanc reussi)
    KitMissing,    // pas de kit
    KeyMissing,    // pas de cle-publique.txt (le service refuserait tous les appels de l'editeur)
    NodeMissing,   // Node.js n'est pas installe
    NodeTooOld,    // Node.js trop ancien (22 ou plus)
    NotLoggedIn,   // ce PC n'est pas connecte a Cloudflare (ou la connexion a expire)
    Subdomain,     // le compte Cloudflare n'a pas encore son sous-domaine workers.dev
    Account,       // la connexion ouvre plusieurs comptes Cloudflare : lequel ?
    Failed,        // un autre echec (les dernieres lignes)
    Cancelled,     // annule
    Busy,          // un deploiement tourne deja
    StartFailed,   // le programme ne s'est pas lance
};

struct Options {
    std::string kit;              // le dossier du kit (--dossier-kit) ; vide : installedKit
    std::string path;             // le PATH ou chercher Node.js ; vide : searchPath()
    bool        dryRun{false};    // --essai : wrangler deploy --dry-run (rien n'est envoye a Cloudflare)
    double      timeoutSeconds{1800.0};   // une premiere fois (npm ci) prend quelques minutes
};

struct Report {
    Outcome                  outcome{Outcome::Failed};
    int                      exitCode{-1};
    std::string              url;        // l'adresse que le deployeur a ecrite (Ok)
    std::string              reason;     // ce qui s'est passe, en francais
    std::string              next;       // l'etape suivante, en francais
    std::string              link;       // une adresse a ouvrir (https://nodejs.org...) ; vide : aucune
    std::vector<std::string> tail;       // les dernieres lignes de la sortie (deja nettoyees)
    std::string              kit;        // le dossier du kit employe
    double                   seconds{0.0};
    [[nodiscard]] bool ok() const noexcept { return outcome == Outcome::Ok; }
    // Le compte rendu en quelques lignes (la console, le journal de la seance).
    [[nodiscard]] std::string text() const;
};

// Une ligne de la sortie (deja nettoyee : scrub), appelee sur le fil qui lance.
using LineSink = std::function<void(const std::string& line)>;

// Mettre le service en ligne : `node deployer.mjs` dans le dossier du kit. `cancel` (facultatif) l'arrete (l'enfant et
// les siens sont tues). BLOQUANT.
[[nodiscard]] Report deploy(const Options& o, const LineSink& onLine = {}, const std::atomic<bool>* cancel = nullptr);
// Connecter ce PC a Cloudflare : `npx wrangler login` dans le dossier du service (wrangler ouvre le navigateur ; il rend
// la main quand la connexion est faite, refusee, ou trop longue). BLOQUANT.
[[nodiscard]] Report login(const Options& o, const LineSink& onLine = {}, const std::atomic<bool>* cancel = nullptr);
// Un deploiement (ou une connexion) tourne dans ce processus.
[[nodiscard]] bool running() noexcept;
// Arreter ce qui tourne (la fenetre se ferme) : comme Annuler.
void stopAll() noexcept;

// ---------------------------------------------------------------------------------------------------------- la sortie
// Une ligne lisible et sans secret : sans les sequences de couleur ni les caracteres de controle, de l'UTF-8 valide,
// 400 caracteres au plus ; un jeton, un mot de passe, une cle d'API, un << Bearer ... >>, le code ou l'etat d'une
// adresse OAuth, une longue suite opaque : << [masqu\xC3\xA9] >>.
[[nodiscard]] std::string scrub(const std::string& raw);
// Classer une fin (le code de sortie, toute la sortie) ; remplit outcome, reason, next, link.
void classify(Report& r, const std::vector<std::string>& lines, bool cancelled, bool dryRun);

// ---------------------------------------------------------------------------------------------------------- la version
struct VersionCheck {
    bool        known{false};       // /v1/etat a dit une version lisible
    bool        outdated{false};    // a mettre a jour : plus ancien que kServiceRequired, ou que le kit de l'Admin
    bool        belowRequired{false};
    bool        kitNewer{false};
    std::string online;             // la version en ligne
    std::string required;           // kServiceRequired
    std::string kit;                // celle du kit ("" : pas de kit lisible)
    std::string target;             // celle a deployer (la plus recente de required et kit)
};
// `online` : la version de /v1/etat ("" : inconnue) ; `kitVersion` : celle du kit ("" : pas de kit).
[[nodiscard]] VersionCheck checkVersion(const std::string& online, const std::string& kitVersion, const std::string& required = kServiceRequired);
// La phrase du bandeau : << Ton service en ligne est en 1.43.0 : deploie la 1.44.0 pour ... >> ; vide : a jour.
[[nodiscard]] std::string outdatedMessage(const VersionCheck& v);

// ---------------------------------------------------------------------------------------------------------- le reste
// Ouvrir une adresse https:// dans le navigateur (nodejs.org, le tableau de bord de Cloudflare) ; rien d'autre ne
// s'ouvre (pas de fichier, pas de programme). Vide : ouverte ; sinon la phrase.
[[nodiscard]] std::string openLink(const std::string& url);

// Les essais : le kit et le PATH a prendre quand rien n'est donne, et qui ouvre une adresse (vides : les vrais).
struct TestHooks {
    std::string                                         kit;
    std::string                                         path;
    std::function<std::string(const std::string& url)>  openLink;
};
void setTestHooks(TestHooks hooks);

} // namespace license_admin::deployer
