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
//                            registre et des revocations ; 1.32.0 : par licence,
//                            l'emission la plus recente installee (issued)
//      historique.jsonl      1.30.0 : ce qui s'est passe (une ligne JSON par
//                            evenement : import accepte ou refuse et sa raison,
//                            demande d'activation ecrite, changement d'etat
//                            constate) ; 500 lignes au plus, les plus anciennes
//                            partent ; une ligne illisible est ignoree
//  Un dossier ou ce compte ne peut pas ecrire (un second compte Windows, sans
//  l'installation pour tous les comptes) : l'evaluation marche quand meme (elle
//  ne fait que lire), et `detail` le dit ; l'import et la demande le refusent
//  avec la raison.
//
//  1.30.0 - LES LICENCES PAS ENCORE ACTIVEES : un programme compile sans aucune
//  cle de l'editeur (PublisherKey.hpp vide, et pas de cle d'essai) ne peut rien
//  verifier. Son etat est NotConfigured : TOUT est ouvert (un programme livre
//  sans cle ne bloque jamais personne), l'import est refuse avec une phrase
//  claire, la demande d'activation marche (elle ne depend pas de l'editeur), et
//  rien n'est ecrit dans le dossier par l'evaluation.
//
//  L'EVALUATION (evaluate), dans l'ordre, au premier ecart :
//    registre    signe par une cle de l'editeur, pas plus ancien que le plus
//                recent deja vu (sinon : remplace par un plus ancien -> Invalide) ;
//    licence     absente -> Absente ; tronquee, signature fausse, champ en double,
//                inconnu, type flottant ou service en ligne (pas en version 1),
//                edition ou fonctionnalite inconnue du registre -> Invalide ;
//                pas liee a la cle de ce PC, ou (1.32.0) liee a une autre
//                installation de Windows (l'empreinte du poste) -> Autre PC ;
//                1.32.0 : une emission plus ancienne que la plus recente deja
//                installee (un vieux .xpglic remis) -> Invalide ; dans la liste
//                de revocation (1.32.0 : ou cette emission-la) -> Revoquee ;
//                horloge -> Horloge reculee (seulement pour une licence datee) ;
//                apres sa fin -> Grace pendant offline_grace_days (sauf une
//                evaluation), puis Expiree ; sinon Valide.
//  Valide et Grace : les fonctionnalites de la licence. Tous les autres etats :
//  celles de l'edition sans licence du registre (FREE). Rien ne ferme un projet :
//  Enregistrer et Exporter ne dependent d'aucune fonctionnalite (Licensing.hpp).
//
//  L'HORLOGE : l'heure la plus tardive deja vue (etat.bin, et les dates
//  d'emission des documents signes, ceux livres avec le programme compris) ; si
//  l'heure du PC est en arriere de plus de 24 h (reglable), c'est << Horloge
//  reculee >>. 1.32.0 : une licence est la mais etat.bin manque ou ne se lit
//  plus -> il est RECREE, l'heure la plus tardive reprise de la plus recente
//  de : l'emission de la licence, du registre, des revocations, la derniere
//  ligne de l'historique, l'heure de modification des fichiers du dossier ; une
//  ligne << controle >> de l'historique le dit. Honnetement : un PC sans reseau
//  ne peut pas prouver l'heure ; ceci arrete le retour en arriere ordinaire de
//  l'horloge, et rend plus difficile celui de quelqu'un qui efface l'etat - pas
//  de quelqu'un qui efface tout et remet les dates de ses fichiers. Le vrai
//  garde-temps viendra avec le service en ligne (une heure signee).
//
//  1.34.0 - LA PERIODE D'EVALUATION DE CE PC (StoreOptions::evaluationPeriod) :
//  licences actives (une cle de l'editeur), AUCUN fichier de licence, et un
//  registre accepte qui a une edition d'evaluation (type evaluation, pas
//  retiree ; "EVALUATION" d'abord, sinon la premiere) -> l'evaluation commence
//  a la premiere evaluation qui le constate et dure duration_days de cette
//  edition (0 : 30 jours). Pendant : l'etat Evaluation, les fonctionnalites et
//  les limites de cette edition ; apres : Absente (FREE), evaluationOver.
//  Son debut est garde en plusieurs copies : etat.bin, evaluation.bin (protege
//  comme lui, DPAPI machine ; il garde aussi l'heure la plus tardive vue), une
//  ligne "evaluation" de l'historique et, sous Windows et pour le dossier par
//  defaut seulement, la valeur "evaluation" de HKCU\Software\XPG\Licences.
//  A la lecture, le debut LE PLUS ANCIEN de toutes les copies compte ; une
//  copie qui manque est remise. Elle ne recommence jamais et n'est jamais
//  prolongee (les jours notes au debut ; un registre qui en donne moins les
//  reduit, un registre qui en donne plus ne change rien). Un PC qui a deja eu
//  une licence (etat.bin) n'en recoit pas de nouvelle ; une licence installee
//  passe avant (l'evaluation ne compte plus). L'horloge reculee pendant
//  l'evaluation -> Horloge reculee (comme une licence datee) ; avant son debut,
//  elle ne commence pas tant que l'horloge est en arriere. Rien ne peut noter
//  le debut (aucune copie ne s'ecrit) : l'evaluation est donnee depuis ce
//  lancement, et `detail` dit qu'elle n'est pas gardee d'un lancement a l'autre.
//  Honnetement : quelqu'un qui efface TOUTES les copies (et l'historique)
//  recommence ; ceci arrete la reinstallation et l'effacement d'un fichier.
// =============================================================================
#pragma once

#include "Documents.hpp"
#include "SignedFile.hpp"

#include <cstddef>
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
    NotConfigured,     // 1.30.0 - Non activees : ce programme ne contient aucune cle de l'editeur ; TOUT est ouvert
    Evaluation,        // 1.34.0 - Evaluation : pas de licence ; la periode d'evaluation de CE PC court (depuis le premier
                       // lancement) - les fonctionnalites de l'edition d'evaluation du registre, le temps qu'elle dure
};
// "valid", "grace"... (stable : pour un journal, un essai).
[[nodiscard]] const char* statusKey(LicenseStatus) noexcept;
// "Valide", "Gr\xC3\xA2" "ce"... (l'etiquette affichee).
[[nodiscard]] std::string statusLabel(LicenseStatus);
// Les fonctionnalites de la licence sont-elles ouvertes dans cet etat ?
// 1.34.0 : et pendant l'evaluation (celles de l'edition d'evaluation).
[[nodiscard]] constexpr bool licenseFeaturesApply(LicenseStatus s) noexcept {
    return s == LicenseStatus::Valid || s == LicenseStatus::Grace || s == LicenseStatus::Evaluation;
}
// 1.30.0 : TOUTES les fonctionnalites sont-elles ouvertes (les licences ne sont pas actives) ?
[[nodiscard]] constexpr bool allFeaturesOpen(LicenseStatus s) noexcept { return s == LicenseStatus::NotConfigured; }
// 1.30.0 : un etat sans anomalie (Valide, Grace, Absente, Non activees ; 1.34.0 : Evaluation) - pour l'historique.
[[nodiscard]] constexpr bool statusIsNormal(LicenseStatus s) noexcept {
    return s == LicenseStatus::Valid || s == LicenseStatus::Grace || s == LicenseStatus::Absent || s == LicenseStatus::NotConfigured ||
           s == LicenseStatus::Evaluation;
}

// Une cle de l'editeur que ce programme croit.
struct TrustedKey {
    std::string       id;      // keyIdOf(key)
    std::string       name;
    crypto::PublicKey key{};
    bool              test{false};
};
// Les cles compilees : les VRAIES (PublisherKey.hpp), plus la cle d'ESSAI
// (TestPublisherKey.hpp) seulement si la macro XPG_LICENCES_CLE_ESSAI est definie
// (l'option CMake du meme nom). Vide : les licences ne sont pas actives (NotConfigured).
[[nodiscard]] std::vector<TrustedKey> compiledPublisherKeys();
// La cle d'essai est-elle compilee dans ce programme (XPG_LICENCES_CLE_ESSAI) ?
[[nodiscard]] bool testKeyCompiled() noexcept;
// 1.32.0 (M1) : le dossier des licences donne par XPG_LICENCES_DOSSIER (environnement) - SEULEMENT dans une
// construction d'essai (XPG_LICENCES_CLE_ESSAI : captures, essais) ; ailleurs, toujours vide (la lecture de la
// variable n'est pas compilee : personne ne remplace le dossier, ni son etat protege, d'un programme livre).
[[nodiscard]] std::string testFolderOverride();

// 1.30.0 : un evenement de l'historique de ce PC (historique.jsonl).
struct HistoryEvent {
    std::int64_t at{0};    // l'heure (secondes UTC)
    std::string  kind;     // "import", "demande", "etat" ; 1.34.0 : "evaluation" (son debut), "passerelle"
    bool         ok{false};   // import accepte, demande ecrite ; etat : sans anomalie (statusIsNormal)
    std::string  text;     // une phrase (en francais) : ce qui a ete importe, la raison du refus...
};

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
    Seconds       expiresAt{0};       // 0 : jamais ; 1.34.0 : pendant l'evaluation, sa fin (type Evaluation)
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
    // 1.34.0 - L'EVALUATION DE CE PC (sans licence, licences actives) : commencee au premier lancement, elle dure
    // ce que dit l'edition d'evaluation du registre. evaluationStartedAt : 0 si elle n'a jamais commence (pas
    // d'edition d'evaluation dans le registre, ou une licence a toujours ete la).
    Seconds       evaluationStartedAt{0};
    Seconds       evaluationEndsAt{0};
    int           evaluationDaysLeft{0};  // Evaluation : les jours qui restent (arrondis au jour superieur)
    bool          evaluationOver{false};  // elle a pris fin (l'etat est alors Absente : l'edition sans licence)
    std::string   evaluationEdition;      // l'edition d'evaluation du registre ("EVALUATION")

    // Ouverte ? (NotConfigured : toujours vrai, rien n'est verrouille.)
    [[nodiscard]] bool hasFeature(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<std::int64_t> limit(std::string_view feature, std::string_view name) const noexcept;
    // "2027-03-31" ; vide : pas de fin.
    [[nodiscard]] std::string expirationDate() const { return expiresAt ? isoDate(expiresAt) : std::string(); }
};

struct StoreOptions {
    std::string                    folder;              // le dossier des licences de ce PC (UTF-8)
    std::vector<TrustedKey>        trustedKeys;         // vide : compiledPublisherKeys() (si compiledKeys)
    bool                           compiledKeys{true};  // faux : seulement trustedKeys (les essais : << un programme sans cle >>)
    std::string                    bundledRegistry;     // facultatif : le registre livre avec le programme (.xpgreg)
    std::string                    bundledRevocations;  // 1.32.0 : facultatif, la liste livree avec le programme (.xpgrev)
    std::function<Seconds()>       clock;               // vide : systemNow() (les essais avancent l'heure)
    Seconds                        clockTolerance{kDay};   // le recul de l'horloge tolere
    // 1.32.0 : l'identifiant de l'installation (l'empreinte du poste, KeyFile.hpp) ; vide : systemMachineId().
    // Les essais en donnent un (deux << installations >> dans un meme processus).
    std::string                    machineId;
    // 1.32.0 : l'heure de modification d'un fichier (l'etat recree) ; vide : celle du systeme de fichiers,
    // seulement avec l'horloge du systeme (une horloge d'essai ne se compare pas aux dates des fichiers).
    std::function<Seconds(const std::string& path)> fileTime;
    // 1.34.0 : la periode d'evaluation de ce PC (voir plus haut). Vide : OUI pour un programme qui croit ses
    // cles COMPILEES (trustedKeys vide et compiledKeys : le Studio livre), NON quand l'appelant donne ses propres
    // cles (les essais d'avant la 1.34.0, les outils) ; un essai de l'evaluation la met a vrai.
    std::optional<bool>            evaluationPeriod;
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
    static constexpr const char* kHistoryFile = "historique.jsonl";
    static constexpr const char* kEvaluationFile = "evaluation.bin";   // 1.34.0 : une copie du debut de l'evaluation
    static constexpr std::size_t kHistoryMaxLines = 500;

    [[nodiscard]] const std::string& folder() const noexcept { return opt_.folder; }
    // 1.30.0 : une cle de l'editeur au moins ? Faux : NotConfigured (tout est ouvert).
    [[nodiscard]] bool configured() const noexcept { return !opt_.trustedKeys.empty(); }
    // 1.34.0 : la periode d'evaluation est-elle donnee sur ce PC (StoreOptions::evaluationPeriod) ?
    [[nodiscard]] bool evaluationPeriod() const noexcept { return evaluation_; }

    // 1.30.0 : ce que la derniere evaluation a lu (l'ecran Licence ; aucun fichier relu) :
    // le registre en vigueur, la licence installee lisible (signee, meme finie, revoquee
    // ou d'un autre PC), la cle publique de ce PC (64 hexadecimaux ; vide : pas encore creee).
    [[nodiscard]] const std::optional<Registry>& registry() const noexcept { return registry_; }
    [[nodiscard]] const std::optional<License>& license() const noexcept { return license_; }
    [[nodiscard]] const std::string& deviceKeyHex() const noexcept { return deviceKeyHex_; }
    // 1.32.0 : l'identifiant de l'installation retenu (StoreOptions::machineId, sinon celui du systeme).
    [[nodiscard]] const std::string& machineId() const noexcept { return opt_.machineId; }

    // 1.30.0 : l'historique de ce PC, le plus recent d'abord (`limit` <= 0 : tout). Lu dans le fichier.
    [[nodiscard]] std::vector<HistoryEvent> history(int limit = 0) const;
    // Ajouter un evenement (`statusKey` : l'etat, pour un evenement "etat"). Faux : `why`.
    bool addHistory(const HistoryEvent& e, std::string_view statusKey, std::string* why);

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

    // ---- 1.34.0 : la passerelle GitHub (licensing/Gateway.hpp, app/LicenceGateway) ----
    // La demande d'activation de ce PC, en TEXTE (le bloc signe, comme le fichier .xpgreq) ; la cle du PC est
    // creee si besoin ; `parsed` : ce qu'elle dit (le nonce...). L'historique n'est pas touche (l'appelant le
    // note, une fois la demande envoyee).
    bool activationRequestText(const std::string& deviceName, const std::string& licenseId, const std::string& software,
                               std::string& blockText, ActivationRequest* parsed, std::string* why);
    // Signer un document avec la cle de ce PC (seulement DocKind::Cancel : l'annulation d'une demande en ligne).
    bool deviceSign(DocKind kind, const std::string& document, std::string& blockText, std::string* why);
    // Ouvrir un sceau fait pour la cle de ce PC (l'etat publie par l'editeur). Faux : pas de cle, pas pour elle.
    bool deviceOpenSeal(std::string_view sealedBase64, std::string& plain, std::string* why);
    // Les cles de l'editeur que ce PC croit (pour verifier ce que publie la passerelle).
    [[nodiscard]] const std::vector<TrustedKey>& trustedKeys() const noexcept { return opt_.trustedKeys; }

private:
    struct Loaded;
    [[nodiscard]] Seconds now() const;
    [[nodiscard]] const TrustedKey* trusted(std::string_view keyId) const noexcept;
    bool verifyTrusted(const SignedBlock& b, std::string* why) const;
    LicenseState judge(const Loaded& in, Seconds now) const;
    ImportReport importChecked(std::string_view bytes);
    bool writeRequestFile(const std::string& path, const std::string& deviceName, const std::string& licenseId, const std::string& software,
                          std::string* why);
    bool buildRequest(const std::string& deviceName, const std::string& licenseId, const std::string& software, std::string& blockText,
                      ActivationRequest* parsed, std::string* why);
    ImportReport finishImport(ImportReport rep);
    std::string noteState(const LicenseState& s);
    [[nodiscard]] std::string lastLoggedStatus() const;
    [[nodiscard]] std::string notWritable(const std::string& why) const;
    [[nodiscard]] Seconds fileTime(const std::string& path) const;
    [[nodiscard]] bool systemEvaluationCopy() const;

    StoreOptions            opt_;
    std::optional<Registry> registry_;
    std::optional<License>  license_;
    std::string             deviceKeyHex_;
    std::string             lastStatus_;        // le dernier etat note dans l'historique (statusKey)
    bool                    deferState_{false}; // pendant un import : l'evenement d'etat vient apres celui de l'import
    std::optional<HistoryEvent> pendingState_;
    std::string             pendingStatus_;
    bool                    evaluation_{false};   // 1.34.0 : la periode d'evaluation est donnee
    Seconds                 evaluationMemo_{0};   // 1.34.0 : un debut qu'aucune copie n'a pu garder (ce lancement)
};

} // namespace licensing
