// =============================================================================
//  app/ai/AiNotifications.hpp - 1.21.2 : LES NOTIFICATIONS DE L'ASSISTANT IA
// -----------------------------------------------------------------------------
//  << Un onglet Notifications pour les choses importantes >>, dans le panneau de
//  l'Assistant (AiPanel, a cote de la conversation). Ce module est LE JOURNAL :
//  ce que l'Assistant a fait ou appris pendant que tu regardais ailleurs, et ce
//  qui merite ton attention - une reponse ou une proposition prete, une
//  proposition appliquee ou en echec, une erreur du service, une piece jointe
//  qui ne se lit pas, un classeur reconnu, un modele de fichier enregistre, des
//  souvenirs a valider ou en conflit, un essai du bac a sable, un scan reseau
//  prepare par l'IA, l'analyse IA d'un incident. Jamais une notification par
//  cycle ni par image : chaque source poste un EVENEMENT.
//
//  SUR ENTRE FILS : post() se fait de n'importe quel fil (le fil du fournisseur,
//  le fil d'ecriture des incidents, l'interface) ; un mutex tient le journal, le
//  temps d'une copie. Rien n'y touche un widget.
//
//  UNE CLE (facultative) : une notification de meme cle REMPLACE l'ancienne -
//  elle remonte en tete, redevient non lue, son compteur augmente (<< x3 >>).
//  Lu / non lu ; markRead, markAllRead, remove, clearRead ; unread() et
//  unreadError() (la pastille de l'onglet : rouge s'il y a une erreur non lue).
//  revision() change a chaque modification (le panneau se redessine).
//
//  BORNE : kMax (300) au plus ; au-dela, les plus anciennes LUES partent
//  d'abord, puis les plus anciennes.
//
//  GARDE SUR LE DISQUE : IA/notifications.json (AiPaths.hpp, notificationsFile),
//  relu a l'ouverture (open) SUR UN FIL A PART, ecrit au plus une fois par
//  seconde sur ce meme fil (une ecriture atomique : core/AtomicFile.hpp) -
//  jamais sur le fil de l'interface. close() ecrit ce qui reste (la sortie).
//  Sans open() (les essais, un outil), tout reste en memoire.
//
//  LE BOUTON d'une notification mene a la chose : `action` est un petit lien que
//  le panneau sait suivre (parseAction) - la conversation et le message, la
//  carte de la proposition, la piece jointe dans le contexte de la discussion,
//  la fenetre << Modele de fichier >>, l'onglet Souvenirs de la memoire, le
//  compte rendu du scan, l'incident.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::notifs {

struct Item {
    std::uint64_t id{0};          // donne par post() (stable, meme quand la cle la remplace)
    std::int64_t  at{0};          // l'heure (secondes depuis 1970) ; 0 a la publication : maintenant
    std::string   category;       // un mot ou deux : Proposition, Service, Piece jointe... (devant le titre)
    std::string   title, detail;  // le titre (une ligne) ; le detail (deux lignes montrees)
    std::string   button, action; // le libelle du bouton et ce qu'il ouvre (les liens plus bas) ; vides : pas de bouton
    std::string   tone{"info"};   // "error", "warning", "ok", "info"
    std::string   key;            // vide : aucune ; la meme cle remplace (non lue, count + 1)
    int           count{1};       // les apparitions (<< x3 >>)
    bool          read{false};
    // 1.39.0 : DES DONNEES EN PLUS (un objet JSON, en texte ; vide : aucune) - << tableau >> : un petit tableau montre
    // sous le detail ({"colonnes":[...], "lignes":[{"cellules":[...], "ton":"ok"|"error"|""}], "plus":N}) ; le reste
    // est a celui qui l'a publiee (l'offre modifiee : avant / apres, pour sa fenetre).
    std::string   data;
    bool operator==(const Item&) const = default;
};

inline constexpr std::size_t kMax = 300;

// ---- le journal (sur entre fils) -------------------------------------------------------------------------------------
std::uint64_t post(Item n);                        // rend son identifiant
[[nodiscard]] std::vector<Item> list();            // la plus recente d'abord
[[nodiscard]] std::optional<Item> find(std::uint64_t id);
[[nodiscard]] std::size_t size();
[[nodiscard]] std::size_t unread();
[[nodiscard]] bool unreadError();                  // une erreur non lue (la pastille rouge)
bool markRead(std::uint64_t id, bool read = true); // faux : inconnue, ou deja dans cet etat
std::size_t markAllRead();                         // rend le nombre de notifications marquees
bool remove(std::uint64_t id);
std::size_t clearRead();                           // retire les lues ; rend leur nombre
[[nodiscard]] std::uint64_t revision();

// ---- la garde sur le disque ------------------------------------------------------------------------------------------
// Relit `file` (sur le fil du journal, sans bloquer l'appelant) puis l'ecrit au plus une fois par seconde apres chaque
// changement. Un autre fichier deja ouvert : ferme d'abord. Vide : rien.
void open(const std::string& file);
// Ecrit ce qui reste et arrete le fil (la sortie de l'application). Sans open : rien.
void close();
[[nodiscard]] bool opened();
// Les essais : attendre que le fichier soit relu et que rien n'attende d'etre ecrit (faux : delai depasse).
bool waitIdle(int timeoutMs);
// La derniere ecriture refusee (vide : aucune) - le fichier n'a pas pu etre ecrit (disque plein, droits).
[[nodiscard]] std::string lastError();
// Les essais : tout oublier (le journal vide, ferme).
void resetForTests();

// Le texte du fichier (les plus anciennes d'abord) et sa lecture ; faux et `why` : illisible.
[[nodiscard]] std::string toJson(const std::vector<Item>& oldestFirst);
bool fromJson(std::string_view text, std::vector<Item>& oldestFirst, std::string* why = nullptr);

// ---- les heures, les jours (l'heure locale) --------------------------------------------------------------------------
[[nodiscard]] std::int64_t nowSeconds();
// "Aujourd'hui", "Hier", sinon "mercredi 28 octobre 2026" (le jour de `at` vu depuis `now`).
[[nodiscard]] std::string dayLabel(std::int64_t at, std::int64_t now);
[[nodiscard]] std::string timeLabel(std::int64_t at);   // "14:05"

// ---- le bouton : ce qu'il ouvre --------------------------------------------------------------------------------------
//  "conv:<conversation>#<message>"   la conversation, le message en vue (message < 0 : son dernier echange)
//  "carte:<conversation>#<message>"  la carte de la proposition de ce message (son plan deplie)
//  "piece:<piece jointe>"            sa conversation, le contexte de la discussion deplie sur elle
//  "modele:<piece jointe>"           la fenetre << Modele de fichier >> de ce classeur
//  "souvenirs:<statut>"              Memoire et apprentissage > Souvenirs ("provisoire" : a valider, "conflits")
//  "scan"                            le compte rendu du dernier scan reseau (Communication > Scanner IP)
//  "incident:<dossier>"              IHM > Ressources > Incidents, cet incident choisi
//  "parametres"                      les parametres de l'Assistant
//  "mission:<identifiant>"           1.22.0 : le tableau de bord de la mission (au centre)
[[nodiscard]] std::string toConversation(const std::string& conversation, int message = -1);
[[nodiscard]] std::string toCard(const std::string& conversation, int message);
[[nodiscard]] std::string toAttachment(const std::string& attachmentId);
[[nodiscard]] std::string toFileModel(const std::string& attachmentId);
[[nodiscard]] std::string toSouvenirs(const std::string& status);
[[nodiscard]] std::string toScanReport();
[[nodiscard]] std::string toIncident(const std::string& folderName);
[[nodiscard]] std::string toSettings();
[[nodiscard]] std::string toMission(const std::string& missionId);   // 1.22.0
struct Target {
    std::string kind;     // "conv", "carte", "piece", "modele", "souvenirs", "scan", "incident", "parametres", "mission" ; vide : inconnu
    std::string id;       // la conversation, la piece, le statut, le dossier
    int         message{-1};
};
[[nodiscard]] Target parseAction(std::string_view action);

// ---- les sources d'ailleurs que l'Assistant (une ligne chez elles) ---------------------------------------------------
// Un modele de fichier enregistre (AiFileModelDialog) : son nom, son genre, la piece jointe d'ou il vient.
void fileModelSaved(const std::string& model, const std::string& genre, const std::string& attachmentId, const std::string& file);
// L'analyse d'un incident par l'IA choisie dans l'Assistant (HmiIncidentHost, fil d'ecriture) : faite (`source` : l'IA)
// ou en echec (`error`). Seulement l'analyse par une IA distante - jamais la fiche faite sur le PC a chaque incident.
void incidentAnalysed(const std::string& alarm, const std::string& folderName, bool done, const std::string& source,
                      const std::string& error);

// Un texte d'une ligne : les retours a la ligne et les marques du markdown (*, `, #) blanchis, coupe a `max` octets
// (jamais au milieu d'un caractere) avec << ... >>.
[[nodiscard]] std::string oneLine(std::string_view text, std::size_t max = 160);

} // namespace app::ai::notifs
