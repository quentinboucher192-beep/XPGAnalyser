// =============================================================================
//  licensing/SignedFile.hpp - 1.30.0 (prepare en 1.29.0) : le fichier signe
// -----------------------------------------------------------------------------
//  Licence (.xpglic), registre (.xpgreg), liste de revocation (.xpgrev) et
//  demande d'activation (.xpgreq) ont la meme enveloppe : un fichier texte fait
//  d'un ou de plusieurs BLOCS, chacun
//
//      XPG-LICENSING 1 <genre> <taille>\n        l'en-tete (genre : licence,
//      <exactement <taille> octets : le JSON>       registre, revocations, demande)
//      SIGNATURE ed25519 <cle> <128 hexa>\n      la signature
//
//  LE JSON EST SIGNE OCTET POUR OCTET, tel qu'il est dans le fichier (comme le
//  manifeste d'un paquet de deploiement) : il n'est jamais reecrit avant d'etre
//  verifie. Le message signe est "XPG-LICENSING-1\n<genre>\n" suivi du JSON :
//  le prefixe separe ces signatures de toute autre faite par la meme cle (un
//  JSON de licence ne passe pas pour un registre, ni pour autre chose).
//  <cle> : l'identifiant de la cle qui a signe ("xpg-" et les 16 premiers
//  hexadecimaux du SHA-256 de la cle publique) - le verifieur la cherche parmi
//  les cles qu'il croit (la cle de l'editeur compilee : PublisherKey.hpp), la
//  demande d'activation parmi... la sienne (elle est signee par le PC).
//
//  La taille dans l'en-tete fait qu'un fichier TRONQUE se voit toujours, avant
//  toute signature. Un fichier qui a change de fins de ligne (CRLF) n'est plus
//  le meme : il est refuse (a transmettre tel quel, en piece jointe).
// =============================================================================
#pragma once

#include "../deploy/Crypto.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace licensing {

namespace crypto = deploy::crypto;

// 1.34.0 : DeviceStatus << poste >> (l'etat d'un PC, signe par l'editeur ; licensing/Gateway.hpp). 1.35.0 : le service
// des licences (licensing/Service.hpp) - ServiceCert << service >> (le certificat de la cle du service, signe par
// l'editeur), et, signes par le SERVICE : Activation << activation >>, Lease << jeton >> (une licence flottante),
// Time << heure >>, Trial << decouverte >>. Ceux-la ne s'importent jamais comme un fichier (licence, registre...).
// (Les genres << passerelle >> et << annulation >> de la 1.34.0 ont disparu en 1.35.0 avec la passerelle : un tel
// bloc est un genre inconnu.)
enum class DocKind : std::uint8_t { License, Registry, Revocations, Request, DeviceStatus, ServiceCert, Activation, Lease, Time, Trial };

// "licence", "registre", "revocations", "demande" (le mot de l'en-tete).
[[nodiscard]] const char* docKindKey(DocKind) noexcept;
// << la licence >>, << le registre >>, << la liste de revocation >>, << la demande d'activation >> (en francais).
[[nodiscard]] std::string docKindLabel(DocKind);
// La taille maximale du JSON d'un genre.
[[nodiscard]] std::size_t docKindMaxBytes(DocKind) noexcept;

inline constexpr std::size_t kMaxSignedFileBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kMaxBlocksPerFile = 8;

struct SignedBlock {
    DocKind           kind{DocKind::License};
    std::string       document;   // le JSON, octet pour octet
    std::string       keyId;      // "xpg-1a2b3c4d5e6f7081"
    crypto::Signature signature{};
};

// L'identifiant d'une cle publique : "xpg-" + 16 hexadecimaux du SHA-256 de la cle.
[[nodiscard]] std::string keyIdOf(const crypto::PublicKey& key);
// Le message reellement signe pour un document de ce genre.
[[nodiscard]] std::string signedMessage(DocKind kind, std::string_view document);

// Signer : `signer` signe un message (la cle secrete ne sort pas de son objet).
using Signer = std::function<crypto::Signature(std::string_view)>;
[[nodiscard]] SignedBlock signBlock(DocKind kind, std::string document, const crypto::PublicKey& pub, const Signer& signer);
// La signature est-elle bonne pour cette cle ?
[[nodiscard]] bool verifyBlock(const SignedBlock& block, const crypto::PublicKey& pub) noexcept;

// Le texte d'un bloc (a mettre bout a bout pour un fichier de plusieurs blocs).
[[nodiscard]] std::string writeBlock(const SignedBlock& block);
// Lire les blocs d'un fichier. Faux : `why` (tronque, en-tete illisible, genre
// inconnu, trop gros, texte en trop...). Rien n'est verifie ici que la forme.
bool readBlocks(std::string_view file, std::vector<SignedBlock>& out, std::string* why);

} // namespace licensing
