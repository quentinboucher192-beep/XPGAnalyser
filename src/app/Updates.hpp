// =============================================================================
//  app/Updates.hpp - 1.44.0 : LES MESSAGES A CE PC ET LES MISES A JOUR DANS LE
//  STUDIO (docs/ADMINS.md, 7 ; maquettes 1.43.0, planches 05 et 06)
// -----------------------------------------------------------------------------
//  Le fil du service des licences (LicenceService) releve, verifie et ouvre ;
//  ici, sur le fil de l'interface, ce que le Studio en fait :
//
//  LES MESSAGES (planche 05) : chaque message devient une notification
//  << <titre> >> (le texte, << Avec fichiers >>) ; ses fichiers et ce qu'on en
//  sait vont dans ses donnees (Item::data, genre << message >>) : la carte du
//  fichier (le nom, la taille, la version ; << Signe par l'editeur >> quand son
//  manifeste est verifie, << Empreinte verifiee >> quand il est telecharge et
//  verifie, << recu du service XPG - chiffre >>), Enregistrer..., Installer
//  (l'installateur lance normalement). Un fichier qui ne correspond pas : la
//  carte << Fichier refuse >>, et une notification le dit.
//
//  LES MISES A JOUR (planche 06) : la version publiee, plus recente que ce
//  programme -> UNE notification par version (<< importante >> : a chaque
//  lancement), le toast << Mise a jour X disponible - Voir >> ; << Plus tard >> :
//  la pastille << X disponible >> dans la barre du haut (un clic : la fenetre).
//  << Telecharger et installer >> : le telechargement verifie, le projet ouvert
//  enregistre (s'il a change ; un echec arrete tout et le dit), l'installateur
//  lance en silencieux (/SILENT /SUPPRESSMSGBOXES /NORESTART /RELANCER="<ce
//  Studio>" /RELANCERPROJET="<le projet>" : il rouvre le Studio sur le projet),
//  puis le Studio se ferme proprement 5 s apres (la carte le dit ; Annuler).
//  Ailleurs que sous Windows : la carte dit ce qui serait lance.
//
//  LE PC D'UN ADMIN : l'Admin laisse son jeton d'admin dans son dossier (ce
//  compte : %LOCALAPPDATA%\XPG\LicenseAdmin\jeton-admin.xpgjet) - signe par la
//  cle de l'editeur ou par un admin ACTIF de la liste, sa cle est celle qui
//  signe, pas expire. Avec lui, et un installateur de l'Admin dans la version :
//  la notification des admins (<< Ton Admin 1.44.0 n'est plus a jour >>, les 2
//  fichiers) ; << Tout installer >> telecharge les deux (celui de l'Admin avec
//  l'en-tete X-XPG-Jeton), lance l'installateur de l'Admin (silencieux, attendu)
//  puis celui des Studios (silencieux, relance), et le Studio se ferme.
// =============================================================================
#pragma once

#include "LicenceService.hpp"
#include "Notices.hpp"

#include "../licensing/Admins.hpp"
#include "../ui/Theme.hpp"
#include "../ui/Widget.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app {
class App;
}

namespace app::updates {

namespace ls = app::licence::service;

// ---- ce que rend le fil du service (le fil de l'interface : LicenceService::poll) ----
void received(const std::vector<ls::InboxMessage>& messages, const std::optional<ls::UpdateOffer>& update, bool versionLooked);

// ---- les donnees d'une notification (Item::data) ----
struct NoticeFile {
    std::string   id, name, sha256, version, kind, audience, token;
    std::uint64_t size{0};
    bool          manifestOk{false};   // << Signe par l'editeur >>
    bool          hashOk{false};       // << Empreinte verifiee >> (telecharge et verifie)
    std::string   refused;             // << Fichier refuse >> : pourquoi
};
struct NoticeData {
    std::string              kind;     // "message", "maj"
    std::string              from, text, action;   // un message (action "admin" : devenir admin)
    licensing::Seconds       issued{0};
    std::string              version;  // une mise a jour
    bool                     important{false};
    std::vector<std::string> notes;
    std::vector<NoticeFile>  files;
};
[[nodiscard]] std::optional<NoticeData> dataOf(std::string_view data);
[[nodiscard]] std::string writeData(const NoticeData& d);
// La notification a-t-elle des fichiers (le filtre << Avec fichiers >>) ; "1 fichier \xC2\xB7 6,5 Mo" (vide : aucun).
[[nodiscard]] bool hasFiles(const notices::Item& n);
[[nodiscard]] std::string filesLine(const notices::Item& n);
// Le bouton d'une notification : "message:<cle>" (un message : sa fenetre ; la cle de la notification), "maj:<version>".
[[nodiscard]] std::string toMessage(const std::string& key);
[[nodiscard]] std::string toUpdate(const std::string& version);
// Un fichier de la notification devient verifie (hashOk) ou refuse ; la notification << Fichier refuse >> aussi.
void markFile(const std::string& noticeKey, const std::string& fileId, bool ok, const std::string& refused);
// La notification de cette cle ; nul : partie.
[[nodiscard]] std::optional<notices::Item> notice(const std::string& key);

// ---- le jeton d'admin de ce compte (laisse par l'Admin) ----
struct AdminToken {
    bool                         ok{false};
    std::string                  why;       // pourquoi il n'est pas cru (vide : absent)
    licensing::admins::AdminToken token;
    std::string                  base64;    // le bloc, pour l'en-tete X-XPG-Jeton
};
[[nodiscard]] std::string adminFolder();        // Windows : %LOCALAPPDATA%\XPG\LicenseAdmin ; ailleurs : $XDG_CONFIG_HOME/xpg-license-admin
void setAdminFolderForTests(std::string folder);
inline constexpr const char* kAdminTokenFile = "jeton-admin.xpgjet";
[[nodiscard]] AdminToken readAdminToken();
// Le meme controle sur un texte (les essais).
[[nodiscard]] AdminToken checkAdminToken(std::string_view blockText, licensing::Seconds now);

// ---- la mise a jour disponible ----
struct Offer {
    ls::UpdateOffer                 offer;
    bool                            adminPc{false};        // un jeton d'admin cru ET un installateur de l'Admin dans la version
    // 1.49.0 : la sorte de l'Admin de ce PC - "admin" (XPG Admin, les admins) ou "admin_plus" (l'Admin+ : le jeton est signe
    // par une cle de l'editeur) ; chacun ne recoit que la sienne (docs/ADMINS.md, par. 12).
    std::string                     adminKind{"admin"};
    licensing::admins::AdminToken   token;                 // adminPc : le jeton (sa version, son nom)
    bool                            adminOutdated{false};  // l'Admin de ce PC est plus ancien que la version
    std::string                     pcName;                // le nom de ce PC
    [[nodiscard]] std::vector<ls::AnnouncedFile> files() const;   // studios (+ admin sur le PC d'un admin)
    [[nodiscard]] const ls::AnnouncedFile* studios() const;
    [[nodiscard]] const ls::AnnouncedFile* admin() const;
};
[[nodiscard]] std::optional<Offer> available();
// "Ton Admin 1.43.0 n'est plus \xC3\xA0 jour (L\xC3\xA9" "a, PC-COMMERCIAL-02)" ; vide : rien a dire.
[[nodiscard]] std::string adminLine(const Offer& o);
// La pastille de la barre du haut : "1.44.1 disponible" ; vide : pas de pastille.
[[nodiscard]] std::string chipText();
// << Plus tard >> : la pastille reste ; la fenetre se ferme.
void later();
// La derniere version regardee, et ce que le service a dit ("" : aucune publiee) - les scripts.
[[nodiscard]] std::string lastLook();
// Le toast (une fois par annonce) ; vide : rien a montrer.
struct Toast {
    std::string title, text;
    double      shownAt{0};
};
[[nodiscard]] std::optional<Toast> toast();
void dismissToast();

// ---- l'installation ----
enum class Phase : std::uint8_t { Idle, Downloading, Saving, Launching, Countdown, Done, Failed, Cancelled };
struct Install {
    Phase       phase{Phase::Idle};
    std::string version;
    bool        all{false};              // << Tout installer >> (l'Admin aussi)
    float       fraction{0.f};           // le telechargement
    std::string name;                    // le fichier en cours
    std::uint64_t done{0}, total{0};
    bool        downloaded{false}, signature{false}, hash{false}, saved{false};
    std::string project;                 // le nom du projet enregistre (vide : aucun projet ouvert)
    double      closeAt{0};              // Countdown : l'heure de la fermeture (horloge monotone, secondes)
    int         secondsLeft{0};
    std::string message;                 // la phrase (une erreur, ce qui a ete lance, ce qui le serait)
    std::string command;                 // la ligne de l'installateur (ou ce qui serait lance)
};
// << Telecharger et installer >> (all : << Tout installer >>, le PC d'un admin). Faux : rien a installer, deja en cours.
bool beginInstall(App& app, bool all);
void cancelInstall();
[[nodiscard]] Install install();
// Le bouton << Installer >> d'un fichier d'un message (l'installateur lance normalement, apres le telechargement verifie).
// Faux : `why`.
bool installMessageFile(const std::string& noticeKey, const std::string& fileId, std::string* why = nullptr);
// Le bouton << Enregistrer... >> : le fichier verifie (telecharge d'abord s'il le faut), copie ou l'on veut.
bool saveMessageFile(const std::string& noticeKey, const std::string& fileId, std::string* why = nullptr);
// Telecharger (si besoin) un fichier d'une notification, verifie ; le resultat : lastFetch (et la notification).
bool fetchNoticeFile(const std::string& noticeKey, const std::string& fileId, std::string* why = nullptr);
// Le fichier d'une notification en cours de telechargement (vide : aucun) et l'avancement.
[[nodiscard]] std::string fetching(float* fraction = nullptr);
// Le dernier resultat d'un telechargement de fichier de notification (vide : aucun) - les scripts, la fenetre.
[[nodiscard]] std::string lastFetch(bool* ok = nullptr, std::string* path = nullptr);
// Chaque image (App::frame) : le telechargement, l'enregistrement, le lancement, la fermeture.
void tick(App& app);
// A la fermeture : le telechargement arrete.
void shutdown();

// ---- le toast, par-dessus tout (App : apres les ecrans ; UpdateWindows.cpp) ----
void paintToast(gfx::IRenderer& r, const ui::Theme& theme, gfx::Size surface);
// Un clic sur le toast : << Voir >> (la fenetre de la mise a jour), la croix. Consumed : pris.
[[nodiscard]] ui::EventResult handleToast(App& app, const ui::InputEvent& ev);

// ---- les lignes de commande (des essais les verifient) ----
// L'installateur des Studios, silencieux, qui relance `studioExe` sur `project` (vide : sans projet).
[[nodiscard]] std::string silentArguments(const std::string& studioExe, const std::string& project);
// L'installateur de l'Admin, silencieux.
[[nodiscard]] std::string adminArguments();
// Le chemin de ce programme.
[[nodiscard]] std::string selfPath();

} // namespace app::updates
