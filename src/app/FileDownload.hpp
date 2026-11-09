// =============================================================================
//  app/FileDownload.hpp - 1.44.0 : TELECHARGER UN FICHIER DU SERVICE, VERIFIE
//  (docs/ADMINS.md, 6 << Les fichiers >> ; partage par les messages a un PC et
//  les mises a jour)
// -----------------------------------------------------------------------------
//  Un fichier (un installateur...) est sur le service en morceaux de 1 Mio ; ce
//  qu'il doit etre est dit DEUX fois, par des documents signes par la cle de
//  l'editeur : le message ou la version (son identifiant, son nom, sa taille,
//  son SHA-256), et son MANIFESTE (genre << fichier >>, GET
//  /v1/fichiers/manifeste?id=). Les deux doivent dire la meme chose.
//
//  LE TELECHARGEMENT (download, BLOQUANT : un fil de travail) :
//    1. le manifeste, verifie (la cle de l'editeur seulement : jamais un admin)
//       et compare a ce qu'on attend ;
//    2. les morceaux, l'un apres l'autre (GET /v1/fichiers/morceau?id=&n=, n
//       depuis 0), ajoutes a <temp>/XPG/fichiers/<id>/<nom>.part - JAMAIS plus
//       d'un morceau en memoire ; un morceau de la mauvaise taille : refuse ;
//    3. REPRIS : un .part deja la (une coupure, un arret) repart au morceau
//       suivant le dernier entier ;
//    4. la taille et le SHA-256 du fichier entier, compares au manifeste :
//       justes, le .part devient <nom> ; faux, il est EFFACE et le fichier est
//       REFUSE (<< la signature ne correspond pas : il a ete modifie en route >>)
//       - jamais propose, ni enregistre.
//  L'ACCES : un fichier << jeton >> passe &jeton=<32 hexa> (le message le donne) ;
//  un fichier << admins >>, l'en-tete X-XPG-Jeton (le jeton d'admin laisse par
//  l'Admin sur ce PC, en base64 : le bloc a des retours a la ligne).
//  L'ADRESSE : celle du service des licences ; elle ne repond pas (une panne
//  reseau) : les adresses de secours du registre, dans l'ordre (le manifeste est
//  signe par l'editeur : l'adresse ne change rien a ce qui est cru).
//
//  LE FIL DE FOND (start, poll depuis l'interface) : une liste de fichiers, l'un
//  apres l'autre (<< Tout installer >> : les Studios et l'Admin) ; l'avancement,
//  Annuler ; shutdown() a la fermeture coupe la requete et attend le fil.
// =============================================================================
#pragma once

#include "../licensing/Admins.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::filedl {

// Ce qu'on attend (dit par un document signe : le message, la version).
struct Want {
    std::string   id, name, sha256;
    std::uint64_t size{0};
    std::string   version;      // facultatif : l'affichage
    std::string   kind;         // "admin", "studios", "autre" ; vide : pas compare
    std::string   token;        // ?jeton= (32 hexadecimaux) ; vide : aucun
    std::string   adminToken;   // X-XPG-Jeton (le bloc << jetonadmin >> en base64) ; vide : aucun
};

struct Outcome {
    bool                                ok{false};
    bool                                refused{false};      // ne correspond pas (signature, taille, SHA-256) : << Fichier refuse >>
    bool                                cancelled{false};
    bool                                manifestVerified{false};   // signe par l'editeur et conforme a ce qu'on attend
    bool                                hashVerified{false};       // le fichier entier : sa taille et son SHA-256
    std::string                         path;                // le fichier verifie
    std::string                         message;             // une phrase
    licensing::admins::FileManifest     manifest;
};

// <temp>/XPG/fichiers (Windows : %TEMP%\XPG\fichiers) ; les essais en posent un autre (vide : retour au dossier temporaire).
[[nodiscard]] std::string rootFolder();
void setRootForTests(std::string folder);
// <racine>/<id>/<nom> (le nom : safeFileName ; sinon vide).
[[nodiscard]] std::string finalPath(const Want& w);

// Le manifeste (le texte du bloc << fichier >>) : signe par une cle de l'editeur que ce programme croit, et il dit ce
// qu'on attend (identifiant, nom, taille, SHA-256 ; la sorte si elle est dite). Faux : `why` (une phrase sans point).
bool checkManifest(std::string_view blockText, const Want& w, licensing::admins::FileManifest& out, std::string& why);
// << XPG-Setup.exe : la signature ne correspond pas : il a ete modifie en route. Il n'est ni propose, ni enregistre. >>
[[nodiscard]] std::string refusedText(const std::string& name);
// "6,5 Mo", "57 Mo", "820 Ko".
[[nodiscard]] std::string sizeText(std::uint64_t bytes);
// "3F9A 0C4E 81D2 77B0 \xE2\x80\xA6 5E63 7B21 C21E" : le debut et la fin d'un SHA-256, lisibles.
[[nodiscard]] std::string shortHash(std::string_view sha256);

// Deja telecharge ET verifie (la taille et le SHA-256 relus : un fichier change depuis est efface) : son chemin.
[[nodiscard]] std::optional<std::string> verifiedCopy(const Want& w);
// La taille seule (sans relire le SHA-256 : pour l'affichage, chaque image) - le fichier entier est la.
[[nodiscard]] bool presentCopy(const Want& w);

// BLOQUANT (un fil de travail) : le telechargement verifie (voir l'en-tete). `progress` (facultatif) : les octets la,
// sur le total ; `stop` (facultatif) : arreter (le .part reste : la reprise).
[[nodiscard]] Outcome download(const Want& w, const std::function<void(std::uint64_t, std::uint64_t)>& progress = {},
                               const std::atomic<bool>* stop = nullptr);

// ---- le fil de fond ----
struct Progress {
    bool          running{false};
    std::size_t   file{0}, files{0};   // le fichier en cours (depuis 0), sur combien
    std::string   name;
    std::uint64_t done{0}, total{0};   // tous les fichiers
};
// Vrai : parti ; faux : un telechargement est deja en cours. `tag` : qui l'a demande (rendu par take).
bool start(std::vector<Want> files, std::string tag);
[[nodiscard]] bool busy() noexcept;
[[nodiscard]] Progress progress();
void cancel();
// Fini : un resultat par fichier, dans l'ordre (le premier echec arrete la suite) ; pris une fois.
[[nodiscard]] std::optional<std::vector<Outcome>> take(std::string* tag = nullptr);
// A la fermeture : la requete coupee, le fil attendu.
void shutdown();

} // namespace app::filedl
