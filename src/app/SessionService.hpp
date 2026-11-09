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
// =============================================================================
#pragma once

#include "../hmi/HmiModel.hpp"
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
    };
    struct JoinTarget {
        std::string address;
        int         port{session::kPort};
        std::string fingerprint;   // celle de l'annonce (epinglee)
        std::string sessionId;
        std::string sessionName, hostName, machine;
        bool        needsCode{false};
    };
    struct Knock {
        int           conn{0};
        std::string   name, machine, machineId, app, version, address;
        bool          codeOk{false};
        std::uint32_t color{0};
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
    // L'hote : la session s'arrete pour tous. L'equipier : il la quitte.
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

    // ---- ce que l'interface montre ----
    [[nodiscard]] const std::vector<session::Member>& members() const noexcept { return members_; }
    [[nodiscard]] int me() const noexcept { return me_; }
    [[nodiscard]] const session::Member* member(int number) const;
    [[nodiscard]] const std::string& sessionName() const noexcept { return name_; }
    [[nodiscard]] const std::string& hostName() const noexcept { return hostName_; }
    [[nodiscard]] const std::string& code() const noexcept { return code_; }        // l'hote, "Il faut le code"
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

    friend struct SessionServiceAccess;
};

} // namespace app
