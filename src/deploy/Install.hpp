// =============================================================================
//  deploy/Install.hpp - 1.29.0 : les applications installees sur le PC de maintenance
// -----------------------------------------------------------------------------
//  Sous le dossier de l'agent :
//
//    incoming/<sha256>.part         un paquet en cours de reception (blocs verifies, mis bout a bout)
//    incoming/<sha256>.json         ce que PREPARE a annonce (application, version, taille...)
//    Applications/<application>/
//        active.json                la version active (remplace d'un coup : fichier temporaire renomme)
//        versions/<id>/             le contenu de app/ du paquet (le dossier du projet, son lanceur)
//        versions/<id>.json         ce qu'on sait de cette version (paquet, SHA-256, date, PC d'origine)
//    historique.jsonl               les deploiements, une ligne JSON chacun
//    poste.json                     le poste lance par l'agent (pid, heure de creation, version)
//    arret.json                     la demande d'arret au poste (XPG_ARRET_FICHIER), le temps d'un STOP
//    reprise-poste.txt              l'etat du poste ecrit par lui a l'arret (XPG_REPRISE_FICHIER) ;
//                                   l'agent ne l'efface pas (sauf s'il a plus de 10 minutes a un lancement)
//    poste-sortie.log               la sortie du poste (Linux ; 1.32.0 : Windows aussi), refaite a chaque lancement
//
//  <id> est la version du paquet ("V3") ; si une version du meme nom est deja
//  installee avec un AUTRE contenu (le projet a change sans nouvelle version),
//  la nouvelle va dans "V3-2", "V3-3"... : rien n'est jamais ecrase.
//
//  active.json : {"format":"xpg-application-active","version":1,"application":"Armoire_Gaz",
//                 "active":"V3","previous":"V2","activatedAt":<ms>,"autostart":true}
// =============================================================================
#pragma once

#include "../core/Json.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace deploy::install {

// Un nom d'application ou de version utilisable comme nom de dossier : 1 a 100
// caracteres, sans / ni \ ni : ni < > " | ? * ni caractere de controle, ni . ou ..,
// ni point ou espace a la fin, ni nom reserve de Windows. Faux : `why`.
bool validName(std::string_view name, std::string* why);

// Ecrire un fichier d'un coup : un fichier temporaire voisin, puis renomme (remplace l'ancien).
// 1.32.0 : crypto::replaceFile (un nom imprevisible, cree en exclusivite).
bool atomicWrite(const std::string& path, std::string_view text, std::string* why);
bool readJson(const std::string& path, core::json::Value& out, std::string* why);

// 1.32.0 : les liens. Un lien symbolique, ou sous Windows un point d'analyse (jonction, lien, point de
// montage) : l'agent n'y entre jamais - quelqu'un d'autre a pu le poser pour lui faire ecrire ou effacer
// ailleurs que dans son dossier.
[[nodiscard]] bool isLink(const std::string& path);
// Un lien entre `root` (exclu) et `path` (compris), sur ce qui existe deja ? Vrai aussi si `path` n'est
// pas sous `root` (refuse).
[[nodiscard]] bool linkBelow(const std::string& root, const std::string& path);
// Effacer un dossier (et son contenu) ou un fichier. Un lien, ou une jonction, la ou dessous : le lien
// seul est retire, jamais ce qu'il designe. Absent : vrai. Faux : il en reste quelque chose (`why`).
bool removeTree(const std::string& path, std::string* why = nullptr);

struct VersionInfo {
    std::string id;              // le dossier ("V3", "V3-2")
    std::string label;           // la version du paquet ("V3")
    std::string sha256;          // le SHA-256 du paquet
    std::int64_t installedAt{0};
    std::string from;            // le nom du PC du Studio
    std::string publisher;       // l'empreinte de la cle d'editeur
    std::string launcher;        // le lanceur, relatif au dossier ("lancer-poste.cmd")
    std::uint64_t files{0}, bytes{0};
};

struct AppInfo {
    std::string name;
    std::string active;          // l'id de la version active ("" : aucune)
    std::string previous;        // l'id de celle qui etait active avant
    bool autostart{false};
    std::int64_t activatedAt{0};
    std::vector<VersionInfo> versions;   // les plus anciennes d'abord
    [[nodiscard]] const VersionInfo* find(const std::string& id) const;
};

class AppStore {
public:
    explicit AppStore(std::string agentDir);

    [[nodiscard]] std::string root() const;                       // <dossier>/Applications
    [[nodiscard]] std::string appDir(const std::string& app) const;
    [[nodiscard]] std::string versionsDir(const std::string& app) const;
    [[nodiscard]] std::string versionDir(const std::string& app, const std::string& id) const;

    // Les applications installees (triees par nom).
    [[nodiscard]] std::vector<AppInfo> list() const;
    // Une application ; faux si elle n'est pas installee.
    bool load(const std::string& app, AppInfo& out) const;
    // Le dossier libre pour une nouvelle version `label` ("V3", sinon "V3-2"...).
    [[nodiscard]] std::string freeId(const std::string& app, const std::string& label) const;
    // Ecrire la fiche d'une version (avant de renommer son dossier).
    bool writeVersion(const std::string& app, const VersionInfo& v, std::string* why);
    // active.json, remplace d'un coup.
    bool setActive(const std::string& app, const std::string& id, const std::string& previous, bool autostart, std::string* why);
    // La version a reactiver par ROLLBACK : la plus recente installee avant l'active ; vide : aucune.
    [[nodiscard]] std::string olderThanActive(const AppInfo& app) const;
    // Garder `keep` versions (les plus recentes) ; jamais l'active ni `protect`. Rend les ids effaces.
    // 1.32.0 : un lien sur le chemin (l'application, versions/) : rien n'est efface ; une version qui est
    // un lien : le lien seul (removeTree).
    std::vector<std::string> prune(const std::string& app, int keep, const std::vector<std::string>& protect);

private:
    std::string dir_;
};

// L'historique des deploiements (<dossier>/historique.jsonl). 1.32.0 : borne (historique-1.jsonl, l'ancien).
inline constexpr std::uintmax_t kHistoryMaxBytes = 1024u * 1024u;
class History {
public:
    explicit History(std::string agentDir);
    void append(const core::json::Value& entry);
    // Les `count` dernieres entrees (la plus recente a la fin).
    [[nodiscard]] std::vector<core::json::Value> tail(std::size_t count) const;
    [[nodiscard]] std::string path() const;

private:
    std::string dir_;
    mutable std::mutex mutex_;
};

} // namespace deploy::install
