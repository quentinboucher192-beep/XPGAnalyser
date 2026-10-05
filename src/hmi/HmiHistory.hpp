// =============================================================================
//  hmi/HmiHistory.hpp — ce que la marche laisse : alarmes, evenements, mesures
// -----------------------------------------------------------------------------
//  PAS DANS LE PROJET : dans le document. L'historique n'est pas une
//  modification (Ctrl+Z ne doit pas faire revenir une alarme), il ne passe
//  donc pas par les commandes. Il s'enregistre a cote du projet IHM :
//
//      ihm/historique/alarmes.csv     une ligne par alarme terminee
//      ihm/historique/evenements.csv  apparitions, acquittements, connexions...
//      ihm/historique/systeme.csv     le journal de l'IHM (demarrage, erreurs)
//      ihm/historique/mesures.csv     les variables archivees, echantillonnees
//      ihm/historique/audit.csv       lot 13 : le journal d'audit, chaine
//      ihm/historique/comptes.csv     lot 13 : les echecs de connexion, les verrous
//
//  Du CSV (separateur ';', entete) : il s'ouvre dans Excel tel quel. Chaque
//  liste est bornee (Configuration > Historiques : nombre d'entrees, duree de
//  conservation) ; les plus anciennes tombent.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../core/Result.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace hmi {

// Une apparition d'alarme, de bout en bout.
struct AlarmOccurrence {
    Id          alarm{kNoId};
    std::string name, message, group, category;
    int         priority{3};
    std::string appeared, acked, cleared;     // "2026-09-22 07:40:12.350" ; vide : pas (encore)
    std::string ackedBy;
    bool operator==(const AlarmOccurrence&) const = default;
};

// Un evenement (apparition, acquittement, disparition, connexion, recette,
// acces refuse...) ou une ligne du journal systeme.
struct HistoryEvent {
    std::string stamp;
    std::string kind;
    std::string source;
    std::string message;
    std::string user;
    bool operator==(const HistoryEvent&) const = default;
};

// Une mesure d'une variable archivee. `epoch` : secondes depuis 1970 (les
// courbes historiques s'en servent pour placer les points).
struct HistorySample {
    std::string stamp;
    double      epoch{0};
    std::string variable;
    double      value{0};
    bool operator==(const HistorySample&) const = default;
};

// ---- lot 13 : le journal d'audit ------------------------------------------------------
//  QUI A FAIT QUOI, QUAND, OU, AVANT ET APRES, ET POURQUOI : chaque variable
//  ecrite par un geste de l'operateur (un clic, une saisie, un glisser), chaque
//  recette appliquee, acquittement, mise de cote, connexion, refus,
//  verrouillage, mot de passe change, signature. Chaque ligne porte
//  l'empreinte (SHA-256) de la precedente et la sienne : une ligne retouchee,
//  supprimee ou inseree casse la chaine, et verifyAudit le dit.
struct AuditEntry {
    std::string stamp;        // "2026-09-25 14:05:12.350"
    std::string user;         // qui ('' : personne n'etait connecte)
    std::string kind;         // "\xC3\x89" "criture", "Acquittement", "Connexion", "Signature"...
    std::string source;       // ou : "Vue_Pompes/Consigne", "Menu de connexion"
    std::string target;       // quoi : la variable, l'alarme, la recette, le compte
    std::string before, after;
    std::string reason;       // le motif (signature, mise de cote, refus)
    std::string signature;    // "chef" ; "chef + visa admin" ; '' : pas signe
    std::string previous;     // l'empreinte de la ligne d'avant (kAuditGenesis pour la premiere)
    std::string hash;         // SHA-256 (hexa) de `previous` et des champs
    bool operator==(const AuditEntry&) const = default;
};
inline constexpr std::string_view kAuditGenesis = "0000000000000000000000000000000000000000000000000000000000000000";
inline constexpr std::size_t      kAuditMax = 100000;    // au-dela, les plus anciennes tombent
// L'empreinte d'une ligne (son `previous` compris, son `hash` exclu).
[[nodiscard]] std::string auditHash(const AuditEntry&);
// Ajoute une ligne au journal : les champs nettoyes (pas de retour a la ligne),
// son precedent, son empreinte. Rend la ligne telle qu'ajoutee.
const AuditEntry& appendAudit(std::vector<AuditEntry>& list, AuditEntry e);
// La chaine est-elle intacte ? `bad` : la premiere ligne fausse (1 = la premiere).
struct AuditCheck {
    bool        ok{true};
    std::size_t lines{0};
    std::size_t bad{0};
    bool        fromOrigin{true};   // la premiere ligne est la toute premiere (sinon, des lignes sont tombees : la limite)
    std::string message;
};
[[nodiscard]] AuditCheck verifyAudit(const std::vector<AuditEntry>&);

// ---- lot 13 : l'etat des comptes (echecs de suite, verrouillage) ----------------------
struct AccountState {
    std::string login;
    int         failures{0};       // les echecs de suite (une connexion reussie remet a 0)
    std::string lockedAt;          // "2026-09-25 14:05:12.350" ; vide : pas verrouille
    double      lockedUntil{0};    // secondes depuis 1970 ; 0 : jusqu'au deverrouillage
    [[nodiscard]] bool locked(double epochNow) const noexcept {
        return !lockedAt.empty() && (lockedUntil <= 0 || epochNow < lockedUntil);
    }
    bool operator==(const AccountState&) const = default;
};

struct History {
    std::vector<AlarmOccurrence> alarms;
    std::vector<HistoryEvent>    events;
    std::vector<HistoryEvent>    system;
    std::vector<HistorySample>   samples;
    std::vector<AuditEntry>      audit;       // lot 13
    std::vector<AccountState>    accounts;    // lot 13
    [[nodiscard]] bool empty() const noexcept {
        return alarms.empty() && events.empty() && system.empty() && samples.empty() && audit.empty() && accounts.empty();
    }
    [[nodiscard]] AccountState*       account(std::string_view login) noexcept;   // sans casse
    [[nodiscard]] const AccountState* account(std::string_view login) const noexcept;
    // Garde les `settings.maxEntries` dernieres de chaque liste, et rien de plus
    // vieux que `settings.retentionDays` avant `nowStamp` ("2026-09-22 ...").
    void trim(const HistorySettings& settings, std::string_view nowStamp = {});
    bool operator==(const History&) const = default;
};

// "2026-09-22 07:40:12.350" : l'heure du poste, a la milliseconde.
[[nodiscard]] std::string wallStamp();
[[nodiscard]] double      wallEpoch();

// Les fichiers, en memoire (l'archive d'export s'en sert aussi).
[[nodiscard]] std::string historyCsv(const History&, std::string_view which);   // alarmes, evenements, systeme, mesures, audit, comptes
[[nodiscard]] bool        parseHistoryCsv(std::string_view text, std::string_view which, History& into,
                                          std::string* error = nullptr);
inline constexpr std::string_view kHistoryFiles[] = {"alarmes", "evenements", "systeme", "mesures", "audit", "comptes"};

[[nodiscard]] core::Status saveHistory(const History&, const std::string& projectFolder);
[[nodiscard]] History      loadHistory(const std::string& projectFolder, std::vector<std::string>* warnings = nullptr);
// Lot 13 : une ligne d'audit ajoutee tout de suite au fichier (l'en-tete s'il est
// neuf) - une panne ne la perd pas ; l'enregistrement du projet reecrit le tout.
[[nodiscard]] core::Status appendAuditFile(const AuditEntry&, const std::string& projectFolder);

// Un champ de CSV (';'), entre guillemets s'il le faut ; et une ligne decoupee.
// `newlines` : "\n" entre guillemets redevient un retour a la ligne.
[[nodiscard]] std::string              csvField(std::string_view);
[[nodiscard]] std::vector<std::string> csvSplit(std::string_view line, char separator = ';', bool newlines = true);

} // namespace hmi
