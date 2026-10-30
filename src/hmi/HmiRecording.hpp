// =============================================================================
//  hmi/HmiRecording.hpp - 1.14.0 (M4) : LES ENREGISTREMENTS DE L'IHM
// -----------------------------------------------------------------------------
//  "Un enregistrement fait comme si on avait filme l'application qui tournait,
//  et ca rejoue le scenario." Pendant que l'IHM tourne (en simulation ou au
//  poste d'exploitation), un enregistrement garde, au rythme de ses prises (10
//  par seconde par defaut, jamais moins que le cycle de l'IHM), SEULEMENT CE QUI
//  CHANGE :
//    - LES VALEURS : une image complete au depart (toutes les variables IHM, et
//      tout ce que l'ecran lit : variables de l'automate, variables systeme,
//      proprietes d'objets, constantes des vues), puis les changements,
//      horodates ; une image complete toutes les N secondes (5) pour sauter vite
//      a un instant ;
//    - L'ECRAN : la vue (et ses parametres), les popups ouvertes (parametres,
//      place), la langue, le theme, les proprietes d'objets ecrites en marche ;
//    - L'OPERATEUR : le pointeur (dans la vue), les clics, les appuis longs, les
//      saisies validees ;
//    - LES EVENEMENTS : les alarmes (la liste ; apparition, acquittement,
//      disparition), les sons joues (la ressource, le canal), le journal.
//
//  LE FICHIER .xrec : un zip (un outil zip l'ouvre) de deux entrees stockees -
//    entete.json : l'en-tete, lisible (nom, description, date, duree, projet et
//                  version, empreinte du projet, vue de depart, ou, par qui, les
//                  comptes) ; il ne contient aucune valeur. Il est EN PREMIER, et
//                  complete de blancs jusqu'a une place fixe (16 Kio) : la liste
//                  du dossier ne lit que lui (quelques Kio), et l'arret, une
//                  description, un nom le reecrivent SUR PLACE ;
//    piste.bin   : la piste, une suite de BLOCS (un par seconde a peu pres),
//                  chacun compresse (deflate) et chiffre, avec sa longueur, son
//                  CRC et l'instant de sa fin. Un bloc abime ou coupe arrete la
//                  lecture : ce qui precede se relit.
//  Pendant l'enregistrement, le fichier <nom>.xrec.part a deja cette forme : les
//  blocs s'ajoutent a mesure. A l'arret, l'en-tete final s'ecrit a sa place, le
//  repertoire du zip a la fin, et le .part est renomme : rien n'est relu ni
//  recopie. Un enregistrement coupe net (plantage, coupure de courant) laisse son
//  .part : la prochaine lecture du dossier le prend (s'il n'est plus ouvert par
//  personne) et en fait un .xrec, relu jusqu'au dernier bloc entier (l'en-tete
//  le dit : coupe).
//
//  LE MOT DE PASSE : un sel et une empreinte, comme les mots de passe des
//  utilisateurs (HmiCrypto.hpp) - le mot de passe n'est pas dans le fichier. La
//  piste est chiffree avec une CLE PROPRE AU FICHIER, tiree au sort : chaque bloc
//  par un flux HMAC-SHA-256 en mode compteur (un nombre unique par fichier, le
//  numero du bloc, le compteur). Cette cle est dans l'en-tete, chiffree par une
//  cle tiree du mot de passe (SHA-256 itere, un second sel) ; sans mot de passe,
//  elle y est en clair. Donner (ou retirer) le mot de passe a l'arret ne change
//  que l'en-tete ; Mot de passe... (Ressources) tire une cle neuve et rechiffre
//  la piste. CE QUE CA PROTEGE : qui n'a pas le mot de passe ne rejoue pas
//  l'enregistrement et ne lit pas ses valeurs. CE QUE CA NE PROTEGE PAS :
//  l'en-tete (nom, description, date, duree, vues, qui l'a fait) reste lisible ;
//  le fichier peut etre supprime, copie ou abime ; la piste n'est pas signee ;
//  pendant l'enregistrement, un .part commence sans mot de passe a sa cle en
//  clair (il recoit le mot de passe a l'arret).
//
//  UN FICHIER HOSTILE (un .xrec fabrique) ne fait pas allouer sans fin : l'en-tete
//  fait 1 Mio au plus, la piste est lue bloc par bloc (64 Mio decompresses au plus
//  chacun, et pas plus que ce que deflate peut donner), 1 Gio decompresse et 12
//  millions de valeurs en tout (limits()) ; au-dela, le fichier est refuse et dit
//  pourquoi.
//
//  LA RELECTURE (Player) : l'etat a un instant = l'image complete d'avant, puis
//  les changements jusqu'a lui - le meme que celui d'une lecture continue. Une
//  valeur pas encore vue a cet instant (lue plus tard la premiere fois) prend sa
//  premiere valeur. Le moteur (HmiRuntimeRecord.cpp) s'en sert : les objets
//  lisent les valeurs de la piste, la vue et les popups la suivent.
//
//  Sans ecran : hmi_recording_test essaie tout ceci.
// =============================================================================
#pragma once

#include "HmiModel.hpp"
#include "../sim/Value.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hmi::rec {

inline constexpr std::string_view kExtension = ".xrec";
inline constexpr std::string_view kPartSuffix = ".part";          // "<nom>.xrec.part" : un enregistrement en cours
inline constexpr std::string_view kHeaderEntry = "entete.json";
inline constexpr std::string_view kTrackEntry = "piste.bin";
inline constexpr std::string_view kNativeFolder = "ihm/ressources/enregistrements";   // relatif au dossier du projet
inline constexpr int              kFormat = 2;                    // 2 (1.14.0) : la cle du fichier dans l'en-tete ; 1 se relit
inline constexpr double           kKeyframeEvery = 5.0;           // s : une image complete
inline constexpr double           kSpeeds[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};

// ---- le dossier ----------------------------------------------------------------
// Le dossier des enregistrements d'un projet : celui de la configuration (absolu, ou relatif au dossier du
// projet), sinon le dossier natif <projet>/ihm/ressources/enregistrements. "" : pas de dossier (un projet jamais
// enregistre, sans chemin absolu donne).
[[nodiscard]] std::string folderOf(const RecordingSettings&, const std::string& projectFolder);
// Le nom d'un enregistrement (sans extension) : pas vide, pas de / \ : * ? " < > |, pas de . au bout, 120 caracteres
// au plus, pas un nom reserve de Windows (CON, PRN, AUX, NUL, COM1 a COM9, LPT1 a LPT9, meme suivis d'un point).
// UN NOM N'EST JAMAIS UN CHEMIN : chaque fonction de fichier qui prend un nom le verifie. Faux et `why` sinon.
[[nodiscard]] bool validName(std::string_view name, std::string* why = nullptr);
// Un nom propre a partir d'un texte quelconque (les caracteres refuses deviennent _, un nom reserve prend un _ ;
// vide : "Enregistrement").
[[nodiscard]] std::string cleanName(std::string_view text);
// "<dossier>/<nom>.xrec".
[[nodiscard]] std::string pathOf(const std::string& folder, std::string_view name);
// Le nom d'un fichier (sans dossier ni extension) : "D:/x/Essai.xrec" -> "Essai".
[[nodiscard]] std::string nameOfPath(std::string_view path);

// ---- l'en-tete (entete.json) ---------------------------------------------------
struct Header {
    int           format{kFormat};
    std::string   application;                 // "IHM Studio 1.14.0"
    std::string   name, description;
    std::string   date;                        // "2026-10-29 14:02:00" (l'heure du poste)
    double        duration{0};                 // s
    std::string   project, projectVersion;
    std::string   fingerprint;                 // l'empreinte du projet (variables, vues) : un projet change se voit
    std::string   startView;
    std::string   where;                       // "Simulation", ou le nom du poste
    std::string   by;                          // l'utilisateur IHM connecte au depart ('' : personne)
    int           rateHz{10};
    double        keyframeEvery{kKeyframeEvery};
    // le mot de passe (vide : en clair)
    std::string   salt, hash;                  // la verification (passwordHash)
    std::string   keySalt, nonce;              // la cle tiree du mot de passe ; le flux
    std::string   fileKey;                     // format 2 : la cle du fichier (hex) - en clair, ou chiffree si protege
    // les comptes
    std::size_t   frames{0}, keys{0}, clicks{0}, alarms{0}, views{0}, sounds{0}, journal{0}, events{0};
    std::string   viewsShown;                  // "Vue_Station;Popup_Pompe"
    std::string   at40;                        // l'ecran a 40 % de la duree ("Vue_Station + Popup_Pompe") : le detail
    bool          cut{false};                  // coupe net : relu jusqu'au dernier bloc entier
    std::uint64_t trackBytes{0};
    [[nodiscard]] bool locked() const noexcept { return !hash.empty(); }
};
[[nodiscard]] std::string toJson(const Header&);
bool fromJson(std::string_view text, Header& out, std::string* why = nullptr);
// Le mot de passe : un sel et une empreinte neufs, et la cle du fichier `fileKey` (32 octets) chiffree par lui ;
// vide : sans protection (la cle en clair).
void protect(Header&, const std::string& password, const std::string& fileKey);
[[nodiscard]] bool passwordOk(const Header&, std::string_view password);
// La cle qui dechiffre la piste (le bon mot de passe d'un enregistrement protege) ; vide : en clair (format 1).
[[nodiscard]] std::string trackKey(const Header&, const std::string& password);

// ---- la piste ------------------------------------------------------------------
enum class KeyKind : std::uint8_t {
    Variable,     // une variable IHM (case par case : FOUR1.TEMPERATURE)
    Plc,          // une variable de l'automate lue par l'ecran
    Public,       // une variable systeme, une propriete d'objet (SYS.TIME, VUE.OBJET.VISIBLE)
    Qualified,    // une constante qualifiee (REGLAGES.SEUIL, VUE.CONSTANTE)
};
struct KeyDef {
    std::string key;         // en majuscules : ce que le moteur cherche
    std::string shown;       // tel que lu ("Four1.Temperature")
    KeyKind     kind{KeyKind::Variable};
    Id          variable{kNoId};   // une variable IHM : son identifiant stable
    std::string declared;          // ... son type declare
};
struct Change {
    std::uint32_t key{0};
    sim::Value    value;
};
struct Frame {
    double              t{0};
    bool                keyframe{false};   // toutes les valeurs connues a cet instant
    std::vector<Change> changes;
};
struct PopupState {
    std::string view, arguments;
    double      x{0}, y{0};
    bool operator==(const PopupState&) const = default;
};
struct Screen {
    double                  t{0};
    std::string             view, arguments;
    std::vector<PopupState> popups;
    std::string             language, theme;
    [[nodiscard]] bool same(const Screen& o) const {
        return view == o.view && arguments == o.arguments && popups == o.popups && language == o.language && theme == o.theme;
    }
};
struct Override {
    std::string view, object, key, value;
    bool operator==(const Override&) const = default;
};
struct Overrides {
    double                t{0};
    std::vector<Override> list;
};
struct Alarm {
    std::string name, message, group;
    int         priority{3};
    bool        active{true}, acked{false}, ackRequired{true};
    std::string appeared, ackedAt, ackedBy, cleared;
    bool operator==(const Alarm&) const = default;
};
struct Alarms {
    double             t{0};
    std::vector<Alarm> list;
};
struct Pointer {
    double       t{0};
    std::string  view;
    double       x{0}, y{0};
    bool         inside{false};
    std::uint8_t buttons{0};
    [[nodiscard]] bool same(const Pointer& o) const {
        return view == o.view && x == o.x && y == o.y && inside == o.inside && buttons == o.buttons;
    }
};
enum class EventKind : std::uint8_t {
    Click = 1,       // a : "Vue.Objet" ; x, y : dans la vue
    DoubleClick,
    LongPress,
    Input,           // a : "Vue.Objet" ; b : ce qui est valide
    AlarmAppear,     // a : l'alarme ; b : son message
    AlarmAck,        // a : l'alarme ; b : qui
    AlarmClear,      // a : l'alarme
    Sound,           // a : la ressource ; b : le canal (Alarmes, Scripts, Actions, Videos)
    Journal,         // a : le genre ; b : le message
    Note,            // a : ce qui est dit (debut, fin...)
};
struct Event {
    double      t{0};
    EventKind   kind{EventKind::Note};
    std::string a, b;
    double      x{0}, y{0};
};
struct Track {
    std::vector<KeyDef>    keys;
    std::vector<Frame>     frames;
    std::vector<Screen>    screens;
    std::vector<Overrides> overrides;
    std::vector<Alarms>    alarms;
    std::vector<Pointer>   pointers;
    std::vector<Event>     events;
    double                 end{0};          // l'instant de la derniere chose lue
    bool                   closed{false};   // la fin propre a ete lue
    bool                   truncated{false};// un bloc abime ou coupe : relu jusqu'au dernier bloc entier
    std::size_t            blocks{0};
};

// ---- enregistrer -----------------------------------------------------------------
struct RecorderOptions {
    std::string partPath;             // le fichier en cours (<dossier>/<nom>.xrec.part)
    Header      header;               // nom, date, projet... ; protege s'il a un mot de passe (protect)
    std::string password;             // le mot de passe qui chiffre le fichier en cours (vide : en clair)
    double      keyframeEvery{kKeyframeEvery};
    double      blockEvery{1.0};      // s : un bloc ecrit
};
class Recorder {
public:
    Recorder() = default;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;
    ~Recorder();

    bool open(RecorderOptions, std::string* why = nullptr);
    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] const Header& header() const noexcept { return options_.header; }
    [[nodiscard]] const std::string& partPath() const noexcept { return options_.partPath; }

    // UNE PRISE : beginSample, chaque valeur, endSample - seul ce qui a change part (toutes les valeurs connues
    // dans une image complete, toutes les keyframeEvery secondes).
    void beginSample(double t);
    void value(std::string_view key, const sim::Value& v, KeyKind kind = KeyKind::Variable, std::string_view shown = {},
               Id variable = kNoId, std::string_view declared = {});
    void endSample();
    // L'ecran, les proprietes ecrites, les alarmes, le pointeur : gardes s'ils changent. Les alarmes donnent
    // leurs evenements (apparition, acquittement, disparition).
    void screen(Screen);
    void overrides(Overrides);
    void alarms(Alarms);
    void pointer(Pointer);
    void event(Event);
    // Un bloc dans le fichier en cours, toutes les blockEvery secondes (force : maintenant).
    bool flush(double t, bool force = false);

    [[nodiscard]] double        elapsed() const noexcept { return last_; }
    [[nodiscard]] std::size_t   frames() const noexcept { return frames_; }
    [[nodiscard]] std::size_t   eventCount() const noexcept { return events_; }
    [[nodiscard]] std::size_t   keyCount() const noexcept { return keys_.size(); }
    [[nodiscard]] std::uint64_t bytes() const noexcept { return written_ + pending_.size(); }
    [[nodiscard]] std::size_t   clicks() const noexcept { return clicks_; }
    [[nodiscard]] std::size_t   alarmEvents() const noexcept { return alarmEvents_; }
    [[nodiscard]] const std::string& lastError() const noexcept { return error_; }
    [[nodiscard]] bool locked() const noexcept { return options_.header.locked(); }

    // FERMER. keep : l'en-tete final (nom, description, la duree et les comptes d'ici) s'ecrit a sa place, le
    // .part devient `finalPath` - rien n'est relu ni recopie. `password` protege le fichier (vide : sans
    // protection) : seule la cle de l'en-tete change. keep faux : rien n'est garde.
    bool close(double t, bool keep, const std::string& finalPath, const std::string& name, const std::string& description,
               const std::string& password, std::string* why = nullptr, Header* written = nullptr);

private:
    void record(std::string bytes);
    std::uint32_t keyOf(std::string_view key, KeyKind kind, std::string_view shown, Id variable, std::string_view declared);
    RecorderOptions           options_;
    bool                      open_{false};
    std::string               key_;              // la cle du fichier (32 octets, tiree au sort)
    std::size_t               room_{0};          // la place de l'en-tete
    std::uint64_t             trackLocalAt_{0}, trackDataAt_{0};
    std::uint32_t             trackCrc_{0};
    std::uint32_t             stampTime_{0}, stampDate_{0};
    std::vector<std::pair<double, std::string>> shown_;   // l'ecran dans le temps (l'ecran a 40 %)
    std::string               pending_;          // les enregistrements pas encore dans un bloc
    std::uint32_t             seq_{0};
    std::uint64_t             written_{0};
    double                    lastBlock_{0}, last_{0}, lastKeyframe_{-1e9};
    double                    sampleAt_{0};
    bool                      inSample_{false};
    std::unordered_map<std::string, std::uint32_t> index_;
    std::vector<KeyDef>       keys_;
    std::vector<sim::Value>   values_;
    std::vector<bool>         known_;
    std::vector<Change>       changes_;
    std::vector<bool>         touched_;
    std::optional<Screen>     screen_;
    std::optional<Overrides>  overrides_;
    std::optional<Alarms>     alarms_;
    std::optional<Pointer>    pointer_;
    std::size_t               frames_{0}, events_{0}, clicks_{0}, alarmEvents_{0}, sounds_{0}, journal_{0};
    std::vector<std::string>  views_;
    std::string               error_;
    void*                     file_{nullptr};    // FILE* du .part
};

// ---- relire ------------------------------------------------------------------------
class Player {
public:
    explicit Player(std::shared_ptr<const Track>);
    [[nodiscard]] const Track& track() const noexcept { return *track_; }
    [[nodiscard]] double length() const noexcept { return track_->end; }
    [[nodiscard]] double position() const noexcept { return pos_; }
    // L'etat a `t` (borne a [0, longueur]) : l'image complete d'avant, puis les changements jusqu'a lui.
    void seek(double t);
    // Avancer : les changements de ]position, t] (en arriere : seek).
    void advanceTo(double t);
    [[nodiscard]] std::optional<std::uint32_t> keyIndex(std::string_view upperKey) const;
    [[nodiscard]] const sim::Value* value(std::uint32_t key) const;
    [[nodiscard]] const sim::Value* valueOf(std::string_view upperKey) const;
    [[nodiscard]] const Screen*    screen() const;      // en vigueur (nul : aucun encore)
    [[nodiscard]] const Overrides* overrides() const;
    [[nodiscard]] const Alarms*    alarms() const;
    [[nodiscard]] const Pointer*   pointer() const;
    // Les evenements de ]a, b] (dans l'ordre).
    [[nodiscard]] std::vector<const Event*> eventsIn(double a, double b) const;
    // Les reperes de la barre : 'a' une alarme (apparition), 'c' un clic (ou appui long), 'v' une vue.
    struct Marker {
        double      t{0};
        char        kind{'c'};
        std::string text;
    };
    [[nodiscard]] std::vector<Marker> markers() const;

private:
    template <class T> [[nodiscard]] const T* at(const std::vector<T>& list) const;
    std::shared_ptr<const Track>                   track_;
    std::vector<sim::Value>                        first_, current_;
    std::unordered_map<std::string, std::uint32_t> index_;
    std::vector<std::size_t>                       keyframes_;   // les rangs des images completes dans frames
    std::size_t                                    cursor_{0};   // frames appliquees : [0, cursor_)
    double                                         pos_{0};
};

// ---- les fichiers ---------------------------------------------------------------------
enum class Status : std::uint8_t { Ok, NotFound, Unreadable, PasswordNeeded, BadPassword };
[[nodiscard]] std::string_view statusText(Status) noexcept;
// L'en-tete seul (la liste, le detail) : pas besoin du mot de passe. Seule l'entree entete.json est lue (quelques
// Kio au debut du fichier ; un zip refait par un autre outil : son repertoire, a la fin).
Status readHeader(const std::string& path, Header& out, std::string* why = nullptr);
// La piste, bloc par bloc : le mot de passe d'un enregistrement protege (vide : PasswordNeeded). Une piste au-dela
// des limites : Unreadable, et `why` le dit.
Status readTrack(const std::string& path, const std::string& password, Header& header, Track& out, std::string* why = nullptr);
// Les limites de la lecture (un fichier fabrique ne fait pas allouer sans fin) ; les essais les baissent.
struct Limits {
    std::uint64_t trackRaw{1ull << 30};      // octets decompresses de la piste, en tout
    std::uint64_t items{12'000'000};         // valeurs, images, evenements... en tout
};
[[nodiscard]] Limits limits() noexcept;
void setLimits(const Limits&) noexcept;
// Ce qui a ete lu des fichiers d'enregistrement depuis le lancement (les essais : la liste ne relit pas tout).
struct FileStats {
    std::uint64_t opens{0}, bytes{0};
};
[[nodiscard]] FileStats fileStats() noexcept;

struct Item {
    std::string   name, path;
    Header        header;
    std::uint64_t bytes{0};
    std::int64_t  modified{0};        // secondes depuis 1970
    std::string   problem;            // illisible : pourquoi ('' : lu)
};
// Les enregistrements d'un dossier, les plus recents d'abord. Un en-tete deja lu (meme chemin, meme taille, meme
// date de modification) ne se relit pas. Un .part abandonne (pas en cours ici, pas touche depuis 10 s, ouvert par
// aucun autre programme : il est pris d'abord) devient un .xrec relu jusqu'au dernier bloc : `notes` le dit.
[[nodiscard]] std::vector<Item> listFolder(const std::string& folder, std::vector<std::string>* notes = nullptr);
// Ceux dont le nom commence par `prefix` (sans la casse, blancs autour retires) ; vide : tous. Le Lecteur
// d'enregistrements : son dessin, ses clics et le moteur prennent les memes lignes.
[[nodiscard]] std::vector<Item> withPrefix(const std::vector<Item>& items, std::string_view prefix);
// Les noms des .xrec du dossier, sans rien lire ni rien recuperer (Generer).
[[nodiscard]] std::vector<std::string> fileNames(const std::string& folder);
// Un .part en .xrec : faux et `why` s'il ne se lit pas, ou s'il est encore ouvert par un autre programme (un autre
// IHM Studio qui enregistre) - il est d'abord pris (verrou, renommage) : deux fenetres n'en font pas deux .xrec.
bool recoverPart(const std::string& partPath, std::string* finalPath = nullptr, std::string* why = nullptr);
// Les .part de ce processus (ceux qu'on ecrit) : la liste du dossier ne les touche pas.
void markActive(const std::string& partPath, bool active);
// Un nom libre dans le dossier ("Essai", "Essai (2)"...).
[[nodiscard]] std::string uniqueName(const std::string& folder, std::string_view wanted);
// Importer un .xrec dans le dossier (son nom, ou un nom libre) ; `name` : le nom pris.
bool importFile(const std::string& source, const std::string& folder, std::string& name, std::string* why = nullptr);
// Exporter (copier) vers un dossier, ou un fichier (.xrec) ; `written` : le chemin ecrit.
struct ExportOptions {
    enum class IfExists : std::uint8_t { Refuse, Replace, FreeName };
    IfExists    ifExists{IfExists::Refuse};   // un fichier deja la : refuse (`exists` vrai), remplace, ou "Nom (2).xrec"
    std::string unprotect;                    // le mot de passe : la copie est sans protection (vide : telle quelle)
    bool*       exists{nullptr};              // Refuse : le fichier etait deja la
    Status*     status{nullptr};              // BadPassword, PasswordNeeded...
};
bool exportFile(const std::string& folder, std::string_view name, const std::string& destination, std::string* written = nullptr,
                std::string* why = nullptr, const ExportOptions& options = {});
bool renameFile(const std::string& folder, std::string_view from, std::string_view to, std::string* why = nullptr);
// Vers la corbeille (un dossier ; vide : supprime).
bool removeFile(const std::string& folder, std::string_view name, const std::string& trash, std::string* why = nullptr);
// Changer le mot de passe (l'actuel s'il y en a un ; le nouveau vide : retirer la protection).
Status setPassword(const std::string& path, const std::string& current, const std::string& fresh, std::string* why = nullptr);
bool setDescription(const std::string& path, const std::string& description, std::string* why = nullptr);
// La taille du dossier au plus : les plus anciens (par la date de l'en-tete, pas par celle du fichier : changer une
// description ne rajeunit rien) sont retires (sauf `keep`) ; rend leurs noms.
std::vector<std::string> prune(const std::string& folder, std::uint64_t maxBytes, std::string_view keep = {});
[[nodiscard]] std::uint64_t folderBytes(const std::string& folder);

// ---- depuis une autre application (la ligne de commande) ---------------------------------
//    IHM-Studio.exe --enregistrer "Nom" [--duree 10m] [--mot-de-passe x]
//    IHM-Studio.exe --arreter-enregistrement [--jeter]
//    IHM-Studio.exe --rejouer "Nom" [--vitesse 2] [--mot-de-passe x]
//    IHM-Studio.exe --quitter-relecture
//  La commande va a IHM Studio deja ouvert (core::instance) ; sinon, avec --poste <projet>, elle ouvre le poste. Il
//  faut que le projet coche Accepter les commandes d'une autre application (Configuration > Enregistrements).
//  Codes de sortie : 0 fait, 2 aucune IHM en marche, 3 nom introuvable, 4 mot de passe faux, 5 refuse par la
//  configuration, 6 occupee (un enregistrement ou une relecture deja en cours), 7 fichier (ecriture impossible,
//  enregistrement illisible, pas de dossier) ; 1 : une option mal ecrite.
inline constexpr int kExitDone = 0, kExitBadOption = 1, kExitNoHmi = 2, kExitNotFound = 3, kExitBadPassword = 4, kExitRefused = 5,
                     kExitBusy = 6, kExitFile = 7;
struct Command {
    enum class Kind : std::uint8_t { None, Record, Stop, Replay, QuitReplay };
    Kind        kind{Kind::None};
    std::string name, password;
    double      duration{0};     // s (0 : celle de la configuration)
    double      speed{1};
    bool        discard{false};  // --jeter
    std::string error;           // une option mal ecrite ('' : rien)
    std::string reply;           // --reponse <fichier> : ou l'instance deja ouverte ecrit sa reponse
};
// Les options de la ligne de commande qui concernent les enregistrements (les autres sont laissees).
[[nodiscard]] Command parseCommand(const std::vector<std::string>& args);
// La ligne de commande a passer a l'instance deja ouverte (avec --reponse).
[[nodiscard]] std::vector<std::string> commandArgs(const Command&);
// Une duree : "10m", "90s", "1h30m", "2h", "600" (des secondes), "00:10:00". Rien : illisible.
[[nodiscard]] std::optional<double> parseDuration(std::string_view);
// La reponse de l'instance : "<code>\n<texte>\n#fin\n" ; et sa lecture (faux tant que la fin n'est pas ecrite).
[[nodiscard]] std::string replyText(int code, std::string_view message);
bool readReply(std::string_view text, int& code, std::string& message);
// LE FICHIER DE LA REPONSE : seulement un fichier NEUF du dossier temporaire, nomme xpg-enregistrement-<chiffres>.txt
// (newReplyPath en fait un) - une commande ne fait jamais ecrire ailleurs. writeReply le cree en exclusivite (deja
// la : refuse).
[[nodiscard]] std::string newReplyPath();
[[nodiscard]] bool        replyPathOk(const std::string& path, std::string* why = nullptr);
bool                      writeReply(const std::string& path, int code, std::string_view message, std::string* why = nullptr);

// ---- les noms cites en dur (Generer, Renommer) -------------------------------------------
// Les noms d'enregistrements ecrits entre apostrophes (ST) ou entre guillemets (scripts C et C++), comme premier
// argument de IHM_REJOUER, IHM_ENREG_EXISTE, IHM_ENREG_DUREE, IHM_ENREG_EXPORTER, IHM_ENREG_SUPPRIMER : le nom et sa
// ligne (1 = la premiere).
struct NameUse {
    std::string name;
    int         line{1};
};
[[nodiscard]] std::vector<NameUse> namesInCode(std::string_view code);
// Le meme code, ces noms-la renommes (sans casse) ; `count` : combien.
[[nodiscard]] std::string renameInCode(std::string_view code, std::string_view from, std::string_view to, std::size_t* count = nullptr);
// Renommer dans le projet : les actions qui le nomment (Rejouer, Exporter, Supprimer), le Lecteur qui le rejoue
// toujours (Enregistrement fixe), les scripts, les fonctions et les actions Executer un script qui le citent en dur.
// Rend le nombre de remplacements.
std::size_t renameReferences(Project&, std::string_view from, std::string_view to);
// Une cible d'action, une propriete : un nom ecrit en dur (ni {formule}, ni $repere$, ni = devant) ?
[[nodiscard]] bool literalName(std::string_view text) noexcept;

// ---- les objets : leurs rectangles (le dessin et les clics) ---------------------------------
struct Area {
    double x{0}, y{0}, w{0}, h{0};
    [[nodiscard]] bool contains(double px, double py) const noexcept { return px >= x && px <= x + w && py >= y && py <= y + h; }
};
// L'Enregistreur : le titre, le bouton rond, son libelle (Enregistrer / Arreter), l'etat (pret, le chrono). Tout
// l'objet est le bouton (un clic : "basculer").
struct RecorderLayout {
    Area   title, button, label, status;
    double cx{0}, cy{0}, radius{0};
};
[[nodiscard]] RecorderLayout recorderLayout(double w, double h, double fontSize, bool titled);
// Le Lecteur : le titre, puis une ligne par enregistrement (son nom, sa duree, sa date, le cadenas) et son bouton
// Rejouer ; "fixed" : un seul grand bouton.
struct PlayerLayout {
    Area              title, list, fixedButton;
    double            rowHeight{28};
    std::size_t       visible{0};
    std::vector<Area> rows, plays;
};
[[nodiscard]] PlayerLayout playerLayout(double w, double h, double fontSize, std::size_t rows, bool fixed, bool titled);
// La partie touchee : "ligne:N", "jouer:N", "jouer" (le bouton de l'enregistrement fixe) ; "" : rien.
[[nodiscard]] std::string playerHit(const PlayerLayout&, double x, double y);

// "mm:ss" (h:mm:ss au-dela d'une heure).
[[nodiscard]] std::string clock(double seconds);
// "12 Ko", "3,4 Mo".
[[nodiscard]] std::string sizeText(std::uint64_t bytes);

} // namespace hmi::rec
