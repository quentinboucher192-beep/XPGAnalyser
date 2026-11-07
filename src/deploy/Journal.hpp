// =============================================================================
//  deploy/Journal.hpp - 1.28.0 : le journal de l'agent
// -----------------------------------------------------------------------------
//  <dossier>/journal/AAAA-MM-JJ.log (la date locale), une ligne par operation :
//      HH:MM:SS op-1a2b3c4d... STATUS ok (PC-DEV 192.168.1.20)
//  "-" a la place de l'identifiant pour ce que l'agent fait de lui-meme
//  (demarrage, arret). Sur entre fils ; GET_LOGS rend les dernieres lignes.
//
//  1.32.0 : borne, contre qui voudrait le remplir depuis le reseau.
//    - aucun caractere de controle (C0, DEL, C1, separateurs de ligne Unicode,
//      controles de sens d'ecriture) : une espace a la place ; un octet qui
//      n'est pas de l'UTF-8 valide : "?" ;
//    - l'identifiant et l'operation : kJournalMaxField octets au plus ; le
//      detail : kJournalMaxDetail (field() borne un champ venu du reseau a
//      kJournalMaxField avant de l'y mettre) ;
//    - kJournalMaxDayBytes par jour : au-dela, une ligne "JOURNAL plein pour
//      aujourd'hui", puis plus rien jusqu'a minuit ;
//    - kJournalMaxFiles fichiers (jours) au plus : les plus anciens s'effacent
//      au premier ecrit, puis a chaque changement de jour.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace deploy {

inline constexpr std::size_t kJournalMaxField = 256;
inline constexpr std::size_t kJournalMaxDetail = 1024;
inline constexpr std::uint64_t kJournalMaxDayBytes = 10ull * 1024 * 1024;
inline constexpr std::size_t kJournalMaxFiles = 30;

class Journal {
public:
    // `maxDayBytes` : la taille d'un jour (les essais la baissent).
    explicit Journal(std::string agentDir, std::uint64_t maxDayBytes = kJournalMaxDayBytes);
    // Ajoute une ligne (nettoyee et bornee : voir l'en-tete).
    void write(std::string_view opId, std::string_view op, std::string_view detail);
    // Les `count` dernieres lignes (le fichier du jour, puis les precedents s'il en manque).
    [[nodiscard]] std::vector<std::string> tail(std::size_t count) const;
    // Le dossier des journaux et le fichier du jour.
    [[nodiscard]] std::string directory() const;
    [[nodiscard]] std::string todayPath() const;

    // Un texte pret pour le journal : sans caractere de controle, UTF-8 valide, `max` octets au plus
    // (coupe : "..." a la fin, compris dans `max`).
    [[nodiscard]] static std::string field(std::string_view s, std::size_t max = kJournalMaxField);

private:
    std::string dir_;
    std::uint64_t maxDay_;
    mutable std::mutex mutex_;
    std::string day_;    // le nom du fichier du dernier ecrit ("" : aucun encore)
    bool full_{false};   // le jour `day_` est plein
};

} // namespace deploy
