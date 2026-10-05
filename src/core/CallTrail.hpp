// =============================================================================
//  core/CallTrail.hpp - 1.10.2 : l'historique interne des appels (le fil d'Ariane)
// -----------------------------------------------------------------------------
//  Le client (02/10, 12 h 41) : « une liste des appels, avec une pile, juste un
//  enorme historique, pour que quand ca crash, ou plante sans raison, on ait
//  acces a cette ressource ».
//
//  Deux choses :
//    * UN TAMPON CIRCULAIRE de kCapacity entrees (l'heure en ms, le fil, le
//      genre, un texte court). Ecrire ne prend aucun verrou et n'alloue rien :
//      un compteur atomique donne la case, un tampon de sequence par case dit
//      au lecteur si la case qu'il copie etait entiere. Les plus vieilles
//      entrees sont ecrasees, c'est voulu.
//    * LA PILE DES PORTEES OUVERTES de chaque fil : XPG_PORTEE("App::openFile")
//      ecrit une entree a l'entree et une a la sortie (avec la duree), et la
//      pile du fil se lit a tout instant, depuis un autre fil (le chien de
//      garde) ou depuis le gestionnaire d'un plantage.
//
//  Le texte d'une portee doit etre une chaine qui vit toujours (un litteral) :
//  la pile garde le pointeur, pas une copie.
//
//  Le lecteur (la fenetre Aide > Journal interne, le rapport de plantage) copie
//  les entrees ; visitRecent() n'alloue rien et ne prend aucun verrou, il sert
//  au gestionnaire d'un plantage.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace core::trail {

// Le genre d'une entree (la colonne « genre » du journal interne).
enum class Kind : std::uint8_t {
    Info,       // une note
    Action,     // une commande ou une action de l'appli (App.cpp, actions_)
    Menu,       // un choix dans un menu
    Dialog,     // une fenetre ou un ecran ouvert / ferme
    Document,   // une commande du document (changeProject, annuler, retablir)
    File,       // ouvrir, enregistrer, importer
    Sim,        // la simulation (demarrer, arreter, les bascules)
    Script,     // un script de l'IHM lance, son erreur
    Build,      // Compiler, Generer
    Warning,    // un avertissement du journal
    Error,      // une erreur du journal
    Enter,      // entree dans une portee (XPG_PORTEE)
    Leave,      // sortie d'une portee (avec sa duree)
    Watchdog,   // le chien de garde (blocage, reprise)
    Crash,      // le plantage lui-meme
};
inline constexpr int kKindCount = 15;

// Le nom court d'un genre, en francais (« action », « menu », « entree »...).
const char* kindName(Kind k) noexcept;

inline constexpr std::size_t kCapacity = 20000;   // entrees du tampon
inline constexpr std::size_t kTextMax  = 100;     // octets de texte (UTF-8, coupe proprement)
inline constexpr std::size_t kMaxDepth = 48;      // portees gardees par fil (au-dela : comptees)
inline constexpr std::size_t kMaxThreads = 64;    // fils suivis
inline constexpr std::size_t kThreadNameMax = 24;

struct Entry {
    std::uint64_t seq = 0;       // numero d'ordre (1, 2, 3...) ; 0 : case vide
    std::uint64_t ms = 0;        // heure UTC, en ms depuis 1970
    std::uint32_t micros = 0;    // Leave : la duree de la portee, en microsecondes
    std::uint16_t thread = 0;    // le numero court du fil (1 = le premier qui a ecrit)
    Kind kind = Kind::Info;
    std::uint8_t depth = 0;      // la profondeur des portees du fil a ce moment
    char text[kTextMax] = {};
};

// ---- ecrire (sans verrou, sans allocation) ----------------------------------------
void note(Kind kind, std::string_view text) noexcept;
// Comme printf, dans un tampon sur la pile (le texte est coupe a kTextMax).
void notef(Kind kind, const char* format, ...) noexcept
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

// Donner un nom au fil appelant (« principal », « simulation », « chien de garde »).
void nameThread(const char* name) noexcept;
// Le numero court du fil appelant (le meme que dans les entrees).
std::uint16_t currentThread() noexcept;

// Tout couper (les mesures du cout se font avec et sans). Vrai par defaut.
void setEnabled(bool on) noexcept;
bool enabled() noexcept;

// ---- la portee : une entree a l'entree, une a la sortie, et la pile du fil ---------
class Scope {
public:
    explicit Scope(const char* name) noexcept;
    Scope(const char* name, std::string_view detail) noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    const char* name_;
    std::uint64_t startUs_;
    bool pushed_;
};

// ---- lire -------------------------------------------------------------------------
// Le nombre total d'entrees ecrites depuis le lancement (les plus vieilles sont perdues
// au-dela de kCapacity).
std::uint64_t written() noexcept;

// Les `max` dernieres entrees, de la plus vieille a la plus recente.
std::vector<Entry> recent(std::size_t max);

// La meme chose sans allocation ni verrou (le gestionnaire d'un plantage) : `fn` recoit
// chaque entree entiere, de la plus vieille a la plus recente.
void visitRecent(std::size_t max, void (*fn)(const Entry&, void*), void* context) noexcept;

// Une portee ouverte d'un fil.
struct Frame {
    const char* name = nullptr;
    std::uint64_t sinceMs = 0;   // l'heure UTC de l'entree, en ms
};
struct ThreadStack {
    std::uint16_t thread = 0;
    char name[kThreadNameMax] = {};
    std::size_t depth = 0;            // la vraie profondeur (peut depasser kMaxDepth)
    std::size_t kept = 0;             // les portees gardees dans frames
    Frame frames[kMaxDepth];
};
// La pile des portees ouvertes de chaque fil vivant (sans allocation : `out` est fourni).
std::size_t stacks(ThreadStack* out, std::size_t max) noexcept;
// Celle d'un seul fil (faux s'il n'existe pas ou s'il est fini).
bool stackOf(std::uint16_t thread, ThreadStack& out) noexcept;

// ---- l'heure ------------------------------------------------------------------------
std::uint64_t nowMs() noexcept;      // UTC, ms depuis 1970
// Le decalage de l'heure locale (ms), lu une fois au lancement puis rafraichi par
// refreshLocalOffset() (le chien de garde le fait) : le gestionnaire d'un plantage
// met une heure en forme sans appeler localtime.
std::int64_t localOffsetMs() noexcept;
void refreshLocalOffset() noexcept;
// « 13:02:11.532 » (heure locale) dans out (au moins 13 octets).
void formatClock(std::uint64_t utcMs, char* out) noexcept;
// « 2026-10-02 13:02:11 » (heure locale) dans out (au moins 20 octets).
void formatDateTime(std::uint64_t utcMs, char* out) noexcept;

// Une entree en une ligne : « 13:02:11.532  #1 principal  action    texte » (sans fin de
// ligne). `out` fait au moins kLineMax octets ; rend la longueur.
inline constexpr std::size_t kLineMax = 256;
std::size_t formatEntry(const Entry& e, char* out, std::size_t size) noexcept;
// Le nom d'un fil d'apres son numero (vide s'il est inconnu) : copie dans out.
void threadName(std::uint16_t thread, char* out, std::size_t size) noexcept;

// Pour les essais : tout vider (aucune autre ecriture ne doit avoir lieu en meme temps).
void resetForTests() noexcept;

} // namespace core::trail

// XPG_PORTEE("App::openFile") ; XPG_PORTEE_TEXTE("App::openFile", chemin)
#define XPG_TRAIL_CAT2(a, b) a##b
#define XPG_TRAIL_CAT(a, b) XPG_TRAIL_CAT2(a, b)
#define XPG_PORTEE(nom) ::core::trail::Scope XPG_TRAIL_CAT(xpgPortee_, __LINE__){nom}
#define XPG_PORTEE_TEXTE(nom, texte) ::core::trail::Scope XPG_TRAIL_CAT(xpgPortee_, __LINE__){nom, texte}
// Une note d'un genre : XPG_TRACE(Action, "Ouvrir %s", nom)
#define XPG_TRACE(genre, ...) ::core::trail::notef(::core::trail::Kind::genre, __VA_ARGS__)
