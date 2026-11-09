// =============================================================================
//  app/SessionService.hpp - 1.43.0 : les sessions dans IHM Studio (docs/SESSIONS.md)
// -----------------------------------------------------------------------------
//  UN PC DEMARRE une session sur le projet ouvert : il en est l'HOTE (son PC garde
//  le projet et l'enregistre). Les autres la REJOIGNENT : le projet arrive une
//  fois, puis seulement les changements, objet par objet (session/SessionSync).
//
//  L'HOTE FAIT FOI : un changement d'un equipier lui arrive d'abord ; il le refuse
//  s'il touche un objet tenu par un autre (ou si l'equipier ne peut que regarder)
//  et renvoie l'etat juste ; sinon il l'applique, signe les objets touches au nom de
//  l'equipier (l'historique des objets : son PC, son identifiant, la session) et le
//  renvoie aux autres. Toutes les 4 s, ses empreintes : un ecart se rattrape.
//
//  CHAQUE IMAGE (tick) : les evenements du reseau ; les changements de ce PC (la
//  vue touchee, le reste du projet) partent ; ce que ce PC choisit (SessionMarks)
//  part, et revient en RESERVATIONS : les objets tenus par les autres.
//
//  Ctrl+Z : la pile de ce PC (hmi::Document::shared : il n'annule que ses gestes).
//
//  1.44.0 : PAR INTERNET (session/SessionRelay) - la meme session, a travers le
//  relais du service XPG (l'adresse du service des licences), chiffree de bout en
//  bout : le code (10 caracteres, "K7Q4-M2XP-T9") est le secret de l'echange CPace ;
//  les appels au relais sont signes par la cle de ce PC (app/SessionRelayHttp).
//  L'entree est celle << il faut le code >> : le code prouve, l'hote fait entrer.
//
//  1.45.0 : RETIRER, PARTIR, LE JOURNAL (docs/SESSIONS.md, paragraphe 8) -
//  - l'hote RETIRE un equipier (remove) : coupe avec sa raison (<< Camille t'a
//    retire de la session >>), les autres le lisent dans la discussion et une
//    notification ; il ne revient pas sans que l'hote le fasse entrer (son PC est
//    retenu : meme avec le code, il attend l'hote) ;
//  - un equipier qui PART (Quitter, le projet ou l'appli fermes, le lien perdu,
//    retire, l'hote qui arrete) revient a l'ACCUEIL : la copie recue est fermee
//    et EFFACEE de son PC (<dossier commun>/Sessions/<identifiant>), jamais dans
//    les projets recents ; un arret brutal : effacee au lancement suivant. Les
//    autres : << Lea a quitte la session >>. L'hote qui ARRETE (le bouton, le
//    projet ou l'appli fermes) coupe tout le monde : << Camille a arrete la
//    session >> ; il reste sur son projet ;
//  - L'EQUIPIER NE GARDE RIEN : Enregistrer sous, Dupliquer, les exports, le
//    dossier du projet sont refuses (SessionMarks::keepRefusal) ; Ctrl+S envoie
//    a l'hote ;
//  - LE JOURNAL (session/SessionJournal) : chaque changement, signe, tenu par
//    l'hote et envoye a tous (JOURNAL) ; exporte en CSV, enregistre avec le projet
//    a la fin (<projet>/ihm/historique/sessions/).
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"
#include "../session/SessionJournal.hpp"   // 1.45.0
#include "../session/SessionProtocol.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

class App;

class SessionService {
public:
    enum class Mode { Off, Host, Joining, Guest };

    struct StartOptions {
        std::string    name;                          // "Mise en service Armoire_Gaz"
        session::Entry entry{session::Entry::Accept};
        session::Role  role{session::Role::Edit};     // ce que font les equipiers en entrant
        int            port{session::kPort};          // les essais : 0 (un port libre)
        int            discoveryPort{session::kDiscoveryPort};
        // 1.44.0 : par Internet (le relais du service XPG) ; `code` : celui montre dans la fenetre (vide : tire ici).
        bool           internet{false};
        std::string    code;
    };
    struct JoinTarget {
        std::string address;
        int         port{session::kPort};
        std::string fingerprint;   // celle de l'annonce (epinglee)
        std::string sessionId;
        std::string sessionName, hostName, machine;
        bool        needsCode{false};
        bool        internet{false};   // 1.44.0 : par Internet (le code de join() : celui de 10 caracteres)
    };
    struct Knock {
        int           conn{0};
        std::string   name, machine, machineId, app, version, address;
        bool          codeOk{false};
        std::uint32_t color{0};
        std::string   fingerprint;   // 1.45.0 : son empreinte (le certificat ; par Internet : la cle de son PC)
        bool          removed{false};   // 1.45.0 : retire tout a l'heure (l'hote le fait entrer lui-meme)
    };
    struct ChatLine {
        std::string   from, text, time;
        std::uint32_t color{0};
        bool          system{false};
    };

    explicit SessionService(App&);
    ~SessionService();
    SessionService(const SessionService&) = delete;
    SessionService& operator=(const SessionService&) = delete;

    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] bool active() const noexcept { return mode_ == Mode::Host || mode_ == Mode::Guest; }
    [[nodiscard]] bool host() const noexcept { return mode_ == Mode::Host; }
    [[nodiscard]] bool guest() const noexcept { return mode_ == Mode::Guest; }

    // ---- l'hote ----
    bool start(const StartOptions&, std::string* why);
    void accept(int conn);
    void refuse(int conn, const std::string& reason);
    void setRole(int member, session::Role role);
    // 1.45.0 : retirer un equipier (coupe, sa raison ; il ne revient que si l'hote le fait entrer).
    void remove(int member);
    // L'hote : la session s'arrete pour tous. L'equipier : il la quitte (il revient a l'accueil, sa copie s'efface).
    void stop(const std::string& reason = {});

    // ---- l'equipier ----
    void search(const std::vector<std::string>& direct = {}, int discoveryPort = session::kDiscoveryPort);
    [[nodiscard]] bool searching() const noexcept;
    [[nodiscard]] const std::vector<session::Found>& found() const noexcept { return found_; }
    [[nodiscard]] const std::string& searchProblem() const noexcept { return searchWhy_; }
    // Appelee (une fois) quand la recherche en cours se termine (le dialogue Rejoindre).
    std::function<void()> onSearchDone;
    bool join(const JoinTarget&, const std::string& code, std::string* why);

    // ---- les deux ----
    void tick();
    void say(const std::string& text);
    // Ouvrir la vue d'un membre (Suivre).
    void follow(int member);
    // 1.45.0 : ce qui attend part tout de suite (Ctrl+S chez l'equipier).
    void flushNow();
    // 1.45.0 : le journal en CSV dans exports/ du projet (l'hote) ; le chemin ecrit (vide : `why`).
    std::string exportJournal(std::string* why);

    // ---- ce que l'interface montre ----
    [[nodiscard]] const std::vector<session::Member>& members() const noexcept { return members_; }
    [[nodiscard]] int me() const noexcept { return me_; }
    [[nodiscard]] const session::Member* member(int number) const;
    [[nodiscard]] const std::string& sessionName() const noexcept { return name_; }
    [[nodiscard]] const std::string& hostName() const noexcept { return hostName_; }
    [[nodiscard]] const std::string& code() const noexcept { return code_; }        // l'hote, "Il faut le code"
    // 1.44.0 : le code tel qu'on le montre ("482 913" ; par Internet "K7Q4-M2XP-T9") ; vide : pas de code.
    [[nodiscard]] std::string codeText() const;
    // 1.44.0 : la session passe par Internet (le relais) ; l'hote : elle est ouverte sur le relais.
    [[nodiscard]] bool internet() const noexcept { return internet_; }
    [[nodiscard]] bool relayOpen() const noexcept { return relayOpen_; }
    // 1.44.0 : ce programme peut-il faire une session par Internet ? (l'adresse du service XPG) Faux : `why`.
    [[nodiscard]] static bool internetAvailable(std::string* why = nullptr);
    [[nodiscard]] session::Entry entry() const noexcept { return entry_; }
    [[nodiscard]] session::Role defaultRole() const noexcept { return defaultRole_; }
    [[nodiscard]] const std::string& progress() const noexcept { return progress_; } // l'equipier qui entre
    [[nodiscard]] const std::vector<Knock>& knocks() const noexcept { return knocks_; }
    [[nodiscard]] const std::vector<ChatLine>& chat() const noexcept { return chat_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] std::string viewName(hmi::Id view) const;
    [[nodiscard]] int port() const;                  // l'hote : le port pris
    [[nodiscard]] int discoveryPort() const;
    [[nodiscard]] std::string myFingerprint() const;
    [[nodiscard]] const std::string& sessionId() const noexcept { return sid_; }
    // La pastille du haut : "Session \xC2\xB7 3" ; vide hors session.
    [[nodiscard]] std::string chipText() const;
    [[nodiscard]] std::string chipTip() const;
    // Les essais : ce qui s'est passe (entrees, refus, rattrapages...).
    [[nodiscard]] const std::vector<std::string>& log() const noexcept { return log_; }
    [[nodiscard]] std::size_t resyncs() const noexcept { return resyncs_; }
    [[nodiscard]] std::size_t refusals() const noexcept { return refusals_; }
    // 1.45.0 : le journal des changements (l'hote le tient, les equipiers en recoivent la copie).
    [[nodiscard]] const session::Journal& journal() const noexcept { return journal_; }
    // 1.45.0 : le journal enregistre avec le projet a la fin (l'hote) ; vide : pas encore, ou rien a ecrire.
    [[nodiscard]] const std::string& savedJournal() const noexcept { return savedJournal_; }
    // 1.45.0 : le dossier des copies des equipiers (<dossier commun>/Sessions) ; un dossier y est-il ?
    [[nodiscard]] static std::string guestArea();
    [[nodiscard]] static bool inGuestArea(const std::string& folder);
    // 1.45.0 : la copie de l'equipier attend d'etre fermee et effacee (la prochaine image) ; le dernier dossier efface.
    [[nodiscard]] bool leaving() const noexcept { return leaving_; }
    [[nodiscard]] const std::string& lastCopy() const noexcept { return lastCopy_; }

    struct Impl;   // le reseau, le miroir, l'etat (SessionService.cpp)

private:
    std::unique_ptr<Impl> d_;
    App& app_;
    Mode mode_{Mode::Off};
    std::vector<session::Member> members_;
    int me_{0};
    std::string name_, hostName_, code_, sid_, progress_, searchWhy_;
    session::Entry entry_{session::Entry::Accept};
    session::Role defaultRole_{session::Role::Edit};
    std::vector<Knock> knocks_;
    std::vector<ChatLine> chat_;
    std::vector<session::Found> found_;
    std::vector<std::string> log_;
    std::uint64_t revision_{0};
    std::size_t resyncs_{0}, refusals_{0};
    bool internet_{false}, relayOpen_{false};   // 1.44.0
    // 1.45.0
    session::Journal journal_;
    std::string savedJournal_;
    bool leaving_{false};          // l'equipier part : sa copie se ferme et s'efface a la prochaine image
    std::string leaveWhy_;         // ... ce que la fenetre dira (vide : rien)
    std::string copyFolder_, copySid_, lastCopy_;
    std::uint64_t boardSeen_{~0ull}, journalSeen_{~0ull}, messagesIn_{0};
    void leaveCopy();
    void publishBoard();

    friend struct SessionServiceAccess;
};

} // namespace app
