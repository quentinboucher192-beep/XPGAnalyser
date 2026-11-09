// =============================================================================
//  session/SessionJournal.hpp - 1.45.0 : LE JOURNAL D'UNE SESSION (qui a change
//  quoi, et quand) ; LE DOSSIER DES COPIES DES EQUIPIERS
// -----------------------------------------------------------------------------
//  docs/SESSIONS.md, paragraphe 8. Le client : << ou se situe l'historique dans les
//  sessions des choses modifiees par tout le monde (a ajouter si inexistant) >>.
//
//  LE JOURNAL : chaque changement applique pendant la session, en une phrase
//  signee (l'heure, l'auteur, sa couleur) : << 14:32 Lea : Vue_Accueil > Texte_Etat
//  deplace >>, << 14:35 Camille : ressource logo.png ajoutee >>. L'HOTE le tient
//  (les changements passent tous par lui) et l'envoie a tous ; un equipier qui
//  entre tard le recoit entier. Borne : 2 000 entrees (les plus anciennes
//  tombent) ; un geste repete (un glisser : 16 envois par seconde) se fond dans
//  l'entree recente du meme auteur et du meme texte (moins de 2 min) : << (x12) >>.
//  A la fin, l'hote l'enregistre avec le projet :
//    <projet>/ihm/historique/sessions/AAAA-MM-JJ_HHhMM_<nom de la session>.csv
//  (le point-virgule, l'UTF-8 avec sa marque : Excel l'ouvre tel quel).
//
//  LES PHRASES : describeView (une vue : ses objets ajoutes, deplaces,
//  redimensionnes, renommes, modifies, supprimes, ses proprietes, l'ordre de
//  dessin ; plus de 3 objets d'un meme geste : << 12 objets deplaces >>) et
//  describeFiles (le reste du projet : l'index ihm.txt lu ligne a ligne - une
//  variable, un script, une ressource... ajoutee, modifiee, supprimee - et les
//  fichiers : le corps d'un script, une ressource entiere).
//
//  LE DOSSIER DES COPIES (les equipiers) : le projet recu arrive dans
//  <dossier commun>/Sessions/<identifiant de la session>/<nom> - a part des
//  projets, jamais dans les projets recents. Il est EFFACE quand l'equipier part
//  (Quitter, retire, l'hote arrete, le lien perdu, le projet ou l'appli fermes),
//  et au lancement suivant ce qu'un arret brutal a laisse (purgeArea).
// =============================================================================
#pragma once

#include "SessionSync.hpp"

#include "../core/Json.hpp"
#include "../hmi/HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace session {

inline constexpr std::size_t  kJournalMax = 2000;     // les entrees gardees
inline constexpr std::int64_t kJournalMerge = 120;    // secondes : un geste repete se fond dans l'entree recente

struct JournalEntry {
    std::uint64_t n{0};          // son numero (donne par l'hote ; une entree fondue garde le sien)
    std::int64_t  t{0};          // l'heure du dernier geste (secondes depuis 1970)
    int           member{-1};    // le membre (0 : l'hote) ; -1 : la session elle-meme
    std::string   who;           // "Lea"
    std::uint32_t color{0};      // 0xRRGGBB
    std::string   text;          // "Vue_Accueil > Texte_Etat deplace"
    int           count{1};      // les gestes fondus dans cette entree
    bool operator==(const JournalEntry&) const = default;
};

class Journal {
public:
    // Une entree (l'hote) : fondue avec une recente du meme membre et du meme texte (parmi les 8 dernieres, moins de
    // kJournalMerge secondes) ; rend l'entree, neuve ou fondue. `t` 0 : maintenant.
    const JournalEntry& add(int member, const std::string& who, std::uint32_t color, const std::string& text, std::int64_t t = 0);
    // Une entree recue de l'hote : neuve, ou elle remplace celle du meme numero.
    void put(const JournalEntry&);
    void replace(std::vector<JournalEntry> all);
    void clear();
    [[nodiscard]] const std::vector<JournalEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_; }   // tombees (la borne)

private:
    void bound();
    std::vector<JournalEntry> entries_;
    std::uint64_t next_{1}, revision_{0};
    std::size_t dropped_{0};
};

// "14:32" (l'heure locale).
[[nodiscard]] std::string clockText(std::int64_t t);
// "14:32 Lea : Vue_Accueil > Texte_Etat deplace" (et " (x3)" si des gestes y sont fondus).
[[nodiscard]] std::string lineOf(const JournalEntry&);
[[nodiscard]] core::json::Value toJson(const JournalEntry&);
bool fromJson(const core::json::Value&, JournalEntry&);
// Le CSV (Excel) : la marque UTF-8, "Date;Heure;Auteur;Changement;Gestes" puis une ligne par entree.
[[nodiscard]] std::string journalCsv(const std::vector<JournalEntry>&);
// Le nom du fichier du journal : "2026-11-09_14h32_Mise en service Armoire_Gaz.csv" (`started` : le debut de la session).
[[nodiscard]] std::string journalFileName(const std::string& sessionName, std::int64_t started);
// Enregistrer le journal avec le projet (l'hote, a la fin) : <projet>/ihm/historique/sessions/<journalFileName>.
// Vide : rien a ecrire (vrai, `path` vide). Faux : `why`.
bool saveJournal(const std::string& projectFolder, const std::string& sessionName, std::int64_t started,
                 const std::vector<JournalEntry>&, std::string* path, std::string* why);
// Ou il est (pour le dire) : "ihm/historique/sessions".
[[nodiscard]] const char* journalFolder() noexcept;

// ---- ce qui a change, en phrases ----
// Une vue : `before` son etat d'avant (les objets d'avant suffisent ; nul : inconnu, tout objet est neuf).
[[nodiscard]] std::vector<std::string> describeView(const ViewDelta&, const hmi::View* before, const std::string& viewName);
// Le reste du projet (sans les vues neuves ou retirees : l'appelant les dit) ; `existed` : le fichier existait-il
// avant (une ressource ajoutee ou remplacee).
[[nodiscard]] std::vector<std::string> describeFiles(const ProjectDelta&, const std::function<bool(const std::string&)>& existed);
// Les phrases d'un changement, sans doublon, 6 au plus (<< ... et 4 autres changements >>).
[[nodiscard]] std::vector<std::string> trimmed(std::vector<std::string> lines);

// ---- le dossier des copies des equipiers ----
// <dossier commun>/Sessions.
[[nodiscard]] std::string guestArea(const std::string& sharedFolder);
// `path` est-il dans l'aire (le dossier lui-meme ou dessous) ?
[[nodiscard]] bool insideArea(const std::string& area, const std::string& path);
// Effacer la copie d'une session (<aire>/<identifiant>). Faux : `why` (absent : vrai).
bool removeCopy(const std::string& area, const std::string& sessionId, std::string* why = nullptr);
// Au lancement : tout ce qui reste dans l'aire, sauf ce que `keep` garde (une copie ouverte dans une autre fenetre).
// Rend le nombre de dossiers effaces.
std::size_t purgeArea(const std::string& area, const std::function<bool(const std::string& folder)>& keep = {});

} // namespace session
