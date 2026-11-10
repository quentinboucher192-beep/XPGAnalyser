// =============================================================================
//  license_admin/ServiceConnection.hpp - 1.47.0 : LA CONNEXION AU SERVICE QUI
//  VOYAGE AVEC L'INSTALLATEUR DE L'ADMIN (docs/ADMINS.md, par. 11)
// -----------------------------------------------------------------------------
//  Le probleme (10/11) : l'admin+ envoie l'Admin a un futur admin ; installe sur
//  son PC, l'Admin n'avait PAS l'adresse du service (service.json absent) :
//  << Envoyer ma demande >> gris, Service en ligne << pas encore regle >>, une
//  adresse a taper a la main (et a croire sur parole).
//
//  LA CONNEXION SIGNEE : le CERTIFICAT DU SERVICE lui-meme (le bloc << service >>,
//  signe par la cle de l'EDITEUR : l'adresse du service, la cle du service, ses
//  dates). Rien de secret (GET /v1/certificat le donne a tous) ; un fichier
//  connexion-service.xpgsvc = ce bloc. Une adresse modifiee (la signature ne va
//  plus), un fichier sans signature, une cle que cet Admin ne connait pas :
//  REFUSES, et la page Service en ligne le dit.
//
//  OU L'ADMIN LA TROUVE, au premier lancement (aucune adresse reglee ; jamais sur
//  le poste de l'admin+, qui regle son service lui-meme), dans l'ordre :
//    1. --connexion F (la ligne de commande) ;
//    2. a cote du programme : connexion-service*.xpgsvc (l'installateur l'y copie
//       quand il est a cote de lui - {src} - ou quand il le porte : construit avec
//       ressources-licences/connexion-service.xpgsvc, outils/mingw/admin_editeur.sh) ;
//    3. le dossier des licences du Studio de ce PC (%ProgramData%\XPG\Licences) :
//       activation.xpgact, decouverte.xpgdec, jeton.xpgjet portent ce certificat
//       des que le Studio a parle au service - c'est le cas du PC qui a recu l'Admin
//       dans les notifications de son Studio.
//  La premiere valable est adoptee : service.json (l'adresse) et
//  connexion-service.json (d'ou elle vient, la cle du service attendue, le bloc).
//  Les cles de l'editeur croyables : celles compilees dans l'Admin
//  (PublisherKey.hpp), celles retenues du service, la sienne (admin+).
// =============================================================================
#pragma once

#include "../licensing/SignedFile.hpp"
#include "../licensing/Time.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace license_admin::connexion {

using licensing::Seconds;

inline constexpr const char* kFileName = "connexion-service.xpgsvc";    // a cote de l'installateur, du programme ; dans le dossier du poste
inline constexpr const char* kStateFile = "connexion-service.json";     // dans le dossier du poste : la connexion adoptee, les refus

struct Connection {
    std::string url;            // l'adresse du service (normalisee)
    std::string serviceKey;     // la cle du service (64 hexadecimaux) que nomme le certificat
    std::string editorKeyId;    // la cle de l'editeur qui l'a signe (xpg-...)
    Seconds     issuedAt{0}, expiresAt{0};
    bool        expired{false}; // le certificat a passe sa date (l'adresse reste celle que l'editeur a signee : a renouveler)
    std::string block;          // le bloc << service >> tel quel (ce qui s'exporte)
    std::string source;         // d'ou elle vient, en francais (<< a cote du programme >>, << le Studio de ce PC (decouverte.xpgdec) >>)
    std::string path;           // le fichier lu
    Seconds     adoptedAt{0};   // adoptee le (connexion-service.json)
};

struct Refused {
    std::string path, source, why;
};

// Lire un texte de blocs signes : le bloc << service >> (le certificat) signe par l'une de `editors`. Faux : `why` - aucun
// bloc << service >> (pas une connexion signee), une signature fausse (le fichier a ete modifie), une cle de l'editeur que
// cet Admin ne connait pas, un certificat illisible, une adresse refusee. `now` : la date (0 : pas regardee).
bool read(std::string_view text, const std::vector<licensing::crypto::PublicKey>& editors, Seconds now, Connection& out, std::string* why);

// Ou chercher (vides : nulle part).
struct Places {
    std::string              explicitFile;   // --connexion F
    std::vector<std::string> beside;         // les dossiers ou chercher connexion-service*.xpgsvc (celui du programme)
    std::string              studioFolder;   // le dossier des licences du Studio de ce PC
};
// Les lieux de l'application : `explicitFile`, le dossier du programme `exeDir`, et sous Windows le dossier des licences
// des Studios (%ProgramData%\XPG\Licences ; LicenseStore::defaultFolder).
[[nodiscard]] Places defaultPlaces(const std::string& exeDir, const std::string& explicitFile);
// 1.47.0 : les lieux de CE processus (l'application et la console les donnent au lancement ; les essais non : un
// Workspace d'essai ne cherche rien). Le Workspace les lit quand aucune adresse n'est reglee.
void setPlaces(Places p);
[[nodiscard]] const Places* places();

struct Discovery {
    bool                     found{false};
    Connection               connection;
    std::vector<Refused>     refused;
    std::vector<std::string> looked;   // les fichiers regardes
};
[[nodiscard]] Discovery discover(const Places& p, const std::vector<licensing::crypto::PublicKey>& editors, Seconds now);

// Adopter dans le dossier du poste : service.json (l'adresse ; jamais par-dessus une adresse deja reglee, sauf `replace`),
// connexion-service.json et connexion-service.xpgsvc. Faux : `why`.
bool adopt(const std::string& folder, const Connection& c, bool replace, Seconds now, std::string* why);
// Noter les refus de la derniere recherche (connexion-service.json garde la connexion adoptee s'il y en a une).
void noteRefused(const std::string& folder, const std::vector<Refused>& refused, Seconds now);
// Ce que dit connexion-service.json : la connexion adoptee (faux : aucune) et les refus notes.
bool readState(const std::string& folder, Connection* adopted, std::vector<Refused>* refused, Seconds* lookedAt = nullptr);
// Le certificat du service verifie a jour (Verifier, l'admin+ ou un admin) : connexion-service.xpgsvc dans le dossier
// du poste (ce qu'on exporte, ce que porte l'installateur). Faux : rien d'ecrit.
bool keepCertificate(const std::string& folder, const std::string& certBlock, std::string* why);
// Le fichier a exporter (le dernier certificat verifie) ; vide : aucun.
[[nodiscard]] std::string exportText(const std::string& folder);

} // namespace license_admin::connexion
