// =============================================================================
//  licensing/Store.hpp - 1.30.0 (prepare en 1.29.0) : les licences de CE PC,
//  verifiees hors ligne
// -----------------------------------------------------------------------------
//  LE DOSSIER (donne par l'appelant ; defaultFolder() en propose un) :
//      identite-pc.cle       la paire de cles du PC (KeyFile.hpp, DPAPI machine)
//      licence.xpglic        la licence installee (le bloc signe, tel que recu)
//      registre.xpgreg       le dernier registre accepte
//      revocations.xpgrev    la derniere liste de revocation acceptee
//      etat.bin              l'etat protege (DPAPI machine) : l'heure la plus
//                            tardive deja vue, les numeros les plus hauts vus du
//                            registre et des revocations
//
//  L'EVALUATION (evaluate), dans l'ordre, au premier ecart :
//    registre    signe par une cle de l'editeur, pas plus ancien que le plus
//                recent deja vu (sinon : remplace par un plus ancien -> Invalide) ;
//    licence     absente -> Absente ; tronquee, signature fausse, champ en double,
//                inconnu, type flottant ou service en ligne (pas en version 1),
//                edition ou fonctionnalite inconnue du registre -> Invalide ;
//                pas liee a la cle de ce PC -> Autre PC ; dans la liste de
//                revocation -> Revoquee ; horloge -> Horloge reculee (seulement
//                pour une licence datee) ; apres sa fin -> Grace pendant
//                offline_grace_days (sauf une evaluation), puis Expiree ;
//                sinon Valide.
//  Valide et Grace : les fonctionnalites de la licence. Tous les autres etats :
//  celles de l'edition sans licence du registre (FREE). Rien ne ferme un projet :
//  Enregistrer et Exporter ne dependent d'aucune fonctionnalite (Licensing.hpp).
//
//  L'HORLOGE : l'heure la plus tardive deja vue (etat.bin, et les dates
//  d'emission des documents signes) ; si l'heure du PC est en arriere de plus de
//  24 h (reglable), c'est << Horloge reculee >>. Honnetement : un PC sans reseau
//  ne peut pas prouver l'heure ; ceci arrete le retour en arriere ordinaire de
//  l'horloge, pas quelqu'un qui efface l'etat et remet ses fichiers - le vrai
//  garde-temps viendra avec le service en ligne (une heure signee).
// =============================================================================
#pragma once

#include "Documents.hpp"
#include "SignedFile.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace licensing {

enum class LicenseStatus : std::uint8_t {
    Valid,             // Valide
    Grace,             // Grace : la licence a pris fin, les fonctionnalites restent ouvertes quelques jours
    Expired,           // Expiree
    Revoked,           // Revoquee
    Invalid,           // Invalide : signature fausse, fichier abime, edition inconnue...
    OtherDevice,       // Autre PC : la licence n'est pas liee a la cle de ce PC
    Absent,            // Absente : aucune licence
    ClockRolledBack,   // Horloge reculee
};
// "valid", "grace"... (stable : pour un journal, un essai).
[[nodiscard]] const char* statusKey(LicenseStatus) noexcept;
// "Valide", "Gr\xC3\xA2" "ce"... (l'etiquette affichee).
[[nodiscard]] std::string statusLabel(LicenseStatus);
// Les fonctionnalites de la licence sont-elles ouvertes dans cet etat ?
[[nodiscard]] constexpr bool licenseFeaturesApply(LicenseStatus s) noexcept { return s == LicenseStatus::Valid || s == LicenseStatus::Grace; }

// Une cle de l'editeur que ce programme croit.
struct TrustedKey {
    std::string       id;      // keyIdOf(key)
    std::string       name;
    crypto::PublicKey key{};
    bool              test{false};
};
// Les cles compilees (PublisherKey.hpp).
[[nodiscard]] std::vector<TrustedKey> compiledPublisherKeys();

// Tout ce qu'il faut pour afficher l'etat de la licence (l'ecran Licence, la barre d'etat).
struct LicenseState {
    LicenseStatus status{LicenseStatus::Absent};
    std::string   edition;            // l'edition EN VIGUEUR : celle de la licence (Valide, Grace), sinon l'edition sans licence
    std::string   editionLabel;       // son nom affiche (registre)
    std::string   licensedEdition;    // l'edition ecrite dans la licence (meme expiree) ; vide sans licence lisible
    std::string   licenseId;
    std::string   customer;
    LicenseType   type{LicenseType::Perpetual};
    int           maxDevices{0};      // de la licence ; sans licence : celui de l'edition sans licence
    int           boundDevices{0};    // les PC lies dans la licence
    Seconds       issuedAt{0};
    Seconds       expiresAt{0};       // 0 : jamais
    Seconds       graceEndsAt{0};     // 0 : pas de grace
    int           graceDaysLeft{0};   // Grace : les jours qui restent (arrondis au jour superieur)
    std::vector<FeatureGrant> features;   // les fonctionnalites OUVERTES (et leurs limites), triees
    std::uint64_t registryNumber{0};      // 0 : pas de registre
    std::uint64_t revocationNumber{0};    // 0 : pas de liste
    std::string   deviceFingerprint;      // l'empreinte de la cle de ce PC (pour l'ecran ; vide : pas encore creee)
    std::string   message;                // UNE PHRASE CLAIRE pour l'utilisateur (en francais, tutoiement)
    std::string   detail;                 // la raison precise (signature fausse, champ en double...) ; vide si rien a dire
    std::string   registryProblem;        // vide : le registre est en ordre
    Seconds       evaluatedAt{0};

    [[nodiscard]] bool hasFeature(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> limit(std::string_view feature, std::string_view name) const noexcept;
    // "2027-03-31" ; vide : pas de fin.
    [[nodiscard]] std::string expirationDate() const { return expiresAt ? isoDate(expiresAt) : std::string(); }
};

struct StoreOptions {
    std::string                    folder;              // le dossier des licences de ce PC (UTF-8)
    std::vector<TrustedKey>        trustedKeys;         // vide : compiledPublisherKeys()
    std::string                    bundledRegistry;     // facultatif : le registre livre avec le programme (.xpgreg)
    std::function<Seconds()>       clock;               // vide : systemNow() (les essais avancent l'heure)
    Seconds                        clockTolerance{kDay};   // le recul de l'horloge tolere
};

// Ce qu'a donne l'import d'un fichier.
struct ImportReport {
    bool                     ok{false};   // tout ce que contenait le fichier a ete accepte (ou etait deja a jour)
    bool                     licenseInstalled{false};
    std::vector<std::string> lines;       // une phrase par document : "Registre n. 318 : install\xC3\xA9."...
    std::string              why;         // le refus, une phrase ; vide si ok
    LicenseState             state;       // l'etat apres l'import
};

class LicenseStore {
public:
    explicit LicenseStore(StoreOptions options);

    // Le dossier propose : Windows %ProgramData%\XPG\Licences ; ailleurs
    // $XDG_DATA_HOME/xpg/licences (ou ~/.local/share/xpg/licences).
    [[nodiscard]] static std::string defaultFolder();
    static constexpr const char* kLicenseFile = "licence.xpglic";
    static constexpr const char* kRegistryFile = "registre.xpgreg";
    static constexpr const char* kRevocationsFile = "revocations.xpgrev";
    static constexpr const char* kStateFile = "etat.bin";

    [[nodiscard]] const std::string& folder() const noexcept { return opt_.folder; }

    // Lire, verifier, dire. Met a jour l'heure la plus tardive vue (etat.bin).
    [[nodiscard]] LicenseState evaluate();

    // Importer un fichier recu de l'editeur : une licence, un registre, une liste
    // de revocation, ou plusieurs dans un meme fichier (celui de l'outil Admin :
    // licence + registre + revocations). Le registre et les revocations passent
    // d'abord ; une licence n'est installee que si elle est valide POUR CE PC
    // (pas pour un autre, pas revoquee, pas expiree, pas plus ancienne que celle
    // deja installee).
    [[nodiscard]] ImportReport importFile(const std::string& path);
    [[nodiscard]] ImportReport importBytes(std::string_view bytes);

    // Ecrire la demande d'activation de ce PC (le fichier a envoyer a
    // l'editeur) ; la cle du PC est creee si elle n'existe pas encore.
    // `deviceName` : un nom CHOISI par l'utilisateur (facultatif ; jamais le nom
    // reseau du PC) ; `licenseId` : la licence demandee (facultatif) ;
    // `software` : "IHM Studio 1.30.0" (facultatif).
    bool writeActivationRequest(const std::string& path, const std::string& deviceName, const std::string& licenseId,
                                const std::string& software, std::string* why);

    // La cle publique de ce PC (64 hexadecimaux), creee si besoin. Faux : `why`.
    bool deviceKey(std::string& hex, std::string* why);

private:
    struct Loaded;
    [[nodiscard]] Seconds now() const;
    [[nodiscard]] const TrustedKey* trusted(std::string_view keyId) const noexcept;
    bool verifyTrusted(const SignedBlock& b, std::string* why) const;
    LicenseState judge(const Loaded& in, Seconds now) const;

    StoreOptions opt_;
};

} // namespace licensing
