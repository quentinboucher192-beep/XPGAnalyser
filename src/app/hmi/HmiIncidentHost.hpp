// =============================================================================
//  app/hmi/HmiIncidentHost.hpp - 1.20.0 : LA SURVEILLANCE DES INCIDENTS
// -----------------------------------------------------------------------------
//  Comme les notifications (HmiNotifyHost) : elle appartient a l'application,
//  pas a un ecran. La simulation IHM et le poste d'exploitation lui donnent les
//  avis d'alarme (Runtime::Hooks::alarmNotice) ; App la fait avancer a chaque
//  image et lui donne l'ecran de la vue en marche.
//
//  CE QUI SE PASSE QUAND UNE ALARME APPARAIT (aucun geste de l'utilisateur) :
//    1. le detecteur (hmi::incident::Monitor) voit le FRONT - jamais un incident
//       a chaque cycle ; le meme front dans le delai anti-rebond : un seul ;
//    2. l'image d'apres le cycle de l'alarme est prise (la capture) ;
//    3. a l'image suivante, sur le fil de l'interface et sans rien bloquer : le
//       contexte COPIE (le graphe des relations, garde tant que le projet en
//       marche ne change pas ; les valeurs ; l'etat des equipements ; la vue) ;
//    4. sur le fil d'ecriture : le dossier d'incident (en attente/), l'analyse
//       initiale (la fiche) ; puis, apres les secondes d'apres, la video ;
//    5. l'analyse par l'IA choisie, en arriere-plan : la reponse est un fichier
//       de plus ; l'IA indisponible : l'incident est garde, l'analyse en attente.
//
//  LE TAMPON CIRCULAIRE : quelques images par seconde de la vue en marche,
//  lues sur le fil de l'interface (une copie de pixels), codees en JPEG sur le
//  fil d'ecriture, gardees les secondes d'avant (borne en memoire).
//
//  JAMAIS un widget touche hors du fil de l'interface ; jamais le projet lu hors
//  de lui. Le fil d'ecriture ne recoit que des donnees copiees.
// =============================================================================
#pragma once

#include "../ai/AiIncident.hpp"
#include "../ai/AiProvider.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gfx { class IRenderer; struct Rect; }

namespace app {

// Ce que la vue en marche donne au moment du defaut (fil de l'interface).
struct IncidentContext {
    const hmi::Project*                project{nullptr};
    std::string                        projectFolder, projectName, projectVersion;
    hmi::incident::ValueReader         read;
    std::vector<hmi::EquipmentStatus>  equipments;
    std::string                        openView;
};

// L'IA qui analyse (celle choisie dans l'Assistant) : le fournisseur et la demande de base (modele, cle, adresse).
struct IncidentAnalyst {
    std::string             provider;      // "anthropic", "openai" ; "integree" : la fiche seule (pas de reseau)
    std::string             label;         // "Anthropic (claude-...)" : la source ecrite dans l'analyse
    app::ai::ProviderRequest base;
};

class IncidentHost {
public:
    IncidentHost();
    ~IncidentHost();
    IncidentHost(const IncidentHost&) = delete;
    IncidentHost& operator=(const IncidentHost&) = delete;

    // ---- le branchement (App) ----
    void setContextSource(std::function<std::optional<IncidentContext>()> f) { source_ = std::move(f); }
    // L'IA a utiliser ; nullopt (et `why`) : pas d'IA distante prete - l'analyse reste en attente.
    void setAnalyst(std::function<std::optional<IncidentAnalyst>(std::string* why)> f) { analyst_ = std::move(f); }
    // Les essais : le fournisseur (nul : makeProvider).
    void setProviderFactory(std::function<std::unique_ptr<app::ai::Provider>(const std::string&)> f) { factory_ = std::move(f); }
    void setProjectFolder(const std::string& folder);   // le dossier du projet (vide : aucun) ; relit les reglages
    [[nodiscard]] const std::string& projectFolder() const noexcept { return projectFolder_; }

    // ---- le fil de l'interface ----
    void notice(const hmi::AlarmNotice&);           // le crochet (la simulation, le poste)
    void tick(double now);                           // chaque image : les fronts, les captures, les analyses, les resultats
    [[nodiscard]] bool wantsFrame(double now) const; // une image a prendre maintenant (la cadence, ou la capture d'un defaut)
    void offerFrame(gfx::IRenderer&, const gfx::Rect& area, double now);   // App::frame, apres le dessin
    // Les essais (sans renderer) : une image RGBA de la vue.
    void offerPixels(std::vector<std::uint8_t> rgba, int w, int h, double now);
    void shutdown();                                 // attend la fin des ecritures (bornee)

    // ---- les reglages (par projet : ihm/ressources/enregistrements/reglages-incidents.json) ----
    [[nodiscard]] const hmi::incident::Settings& settings() const noexcept { return settings_; }
    // 1.20.2 : la surveillance mise en marche ou arretee (Settings::enabled) remet le detecteur a zero et oublie les
    // defauts pas encore enregistres ; arretee, les videos en cours se terminent tout de suite (les images deja la).
    bool setSettings(const hmi::incident::Settings&, std::string* why = nullptr);
    // 1.20.2 : L'INTERRUPTEUR GENERAL (<< arreter totalement la generation des incidents >>) - Settings::enabled, la
    // cle "active" de reglages-incidents.json. setEnabled l'enregistre (comme setSettings) ; faux : rien n'a pu etre
    // ecrit (`why`), le reglage vaut quand meme pour la seance. Arretee : aucun incident, aucune image prise.
    [[nodiscard]] bool enabled() const noexcept { return settings_.enabled; }
    bool setEnabled(bool on, std::string* why = nullptr);

    // ---- la liste (relue sur le fil d'ecriture ; le volet lit la derniere) ----
    struct Listing {
        std::vector<app::ai::incidents::Item> pending, validated;
        std::uint64_t                         revision{0};
    };
    [[nodiscard]] std::shared_ptr<const Listing> listing() const;
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_.load(); }
    void refresh();                                  // relire (en arriere-plan)

    // ---- les gestes (en arriere-plan ; le resultat arrive par events() et la liste) ----
    void comment(const std::string& folder, const std::string& text, const std::string& author);
    void analyze(const std::string& folder);         // une nouvelle analyse (l'IA choisie) ; la fiche si aucune
    void validate(const std::string& folder, const std::string& note, const std::string& author);
    void remove(const std::string& folder);
    [[nodiscard]] app::ai::incidents::Integrity verify(const std::string& folder) const;   // (lecture directe, rapide)

    // L'etat, pour le volet et la barre.
    [[nodiscard]] bool        analysing() const noexcept { return provider_ != nullptr; }
    [[nodiscard]] std::string analysingFolder() const { return current_ ? current_->folder : std::string{}; }
    [[nodiscard]] std::size_t queuedAnalyses() const noexcept { return analyses_.size(); }
    [[nodiscard]] std::size_t writesWaiting() const;
    [[nodiscard]] std::size_t bufferedFrames() const;
    [[nodiscard]] std::size_t bufferedBytes() const;
    [[nodiscard]] const hmi::incident::Monitor& monitor() const noexcept { return monitor_; }
    // Les lignes du journal ("Incident enregistr\xC3\xA9 : ...", une erreur disque...), le plus ancien d'abord.
    [[nodiscard]] std::vector<std::string> takeEvents();
    [[nodiscard]] std::size_t created() const noexcept { return created_; }   // les incidents crees depuis le lancement

    // Les essais : le stockage (les fautes simulees).
    [[nodiscard]] app::ai::incidents::Store* storeForTests() { return store_.get(); }
    // Les essais : attendre que le fil d'ecriture ait tout fait.
    bool drain(int timeoutMs);

private:
    struct Frame {
        std::shared_ptr<const std::vector<std::uint8_t>> jpeg;
        int    w{0}, h{0};
        double t{0};
    };
    struct Trigger {
        hmi::incident::Event event;
        double               at{0};          // l'horloge de l'hote
        std::optional<Frame> capture;        // l'image du defaut (JPEG, venue du fil)
        bool                 waitingCapture{true};
        std::uint64_t        captureSeq{0};
    };
    struct Recording {                       // la video d'un incident : les images d'apres
        std::string id;                      // l'incident (son dossier est connu du fil d'ecriture)
        double      trigger{0}, until{0};
    };
    struct Job {
        std::string             folder;
        hmi::incident::Snapshot snapshot;
    };
    struct Retry {                           // un incident a ecrire (garde en memoire si le disque refuse)
        hmi::incident::Snapshot          snapshot;
        app::ai::incidents::Capture      capture;
        int                              attempts{0};
        std::size_t                      maxPending{200};   // (copie du reglage : le fil d'ecriture ne lit pas settings_)
    };
    void post(std::function<void()> job);
    void worker();
    void postEvent(std::string line);
    void createIncident(Trigger& t, double now);
    void writeIncident(const std::shared_ptr<app::ai::incidents::Store>& store, Retry r);   // (fil d'ecriture)
    void relist(const std::shared_ptr<app::ai::incidents::Store>& store);                    // (fil d'ecriture)
    void startNextAnalysis();
    void pollAnalysis();
    void saveSettings();
    void loadSettings();
    void encodeFrame(std::vector<std::uint8_t> rgba, int w, int h, double t, std::uint64_t seq, double keep);
    [[nodiscard]] std::string root() const;

    hmi::incident::Settings                                   settings_;
    hmi::incident::Monitor                                    monitor_;
    std::function<std::optional<IncidentContext>()>           source_;
    std::function<std::optional<IncidentAnalyst>(std::string*)> analyst_;
    std::function<std::unique_ptr<app::ai::Provider>(const std::string&)> factory_;
    std::string                                               projectFolder_;
    std::shared_ptr<app::ai::incidents::Store>                store_;
    double                                                    now_{0}, lastFrame_{-1e9}, lastEquip_{-1e9}, lastRetry_{0};
    bool                                                      captureNow_{false};
    std::uint64_t                                             frameSeq_{0};
    std::vector<Trigger>                                      triggers_;
    std::vector<Recording>                                    recordings_;
    std::vector<Retry>                                        retries_;
    std::size_t                                               created_{0};
    // le graphe des relations du projet en marche (refait quand il change, jamais a chaque cycle)
    std::shared_ptr<const hmi::rel::Graph>                    graph_;
    const hmi::Project*                                       graphProject_{nullptr};
    std::uint64_t                                             graphPrint_{0};
    // les analyses (fil de l'interface : le fournisseur travaille sur son propre fil)
    std::deque<Job>                                           analyses_;
    std::optional<Job>                                        current_;
    std::unique_ptr<app::ai::Provider>                        provider_;
    std::string                                               answer_, currentSource_, currentHash_, currentModel_;
    double                                                    analysisStarted_{0};
    // le fil d'ecriture
    std::map<std::string, std::string>                        idToFolder_;  // (fil d'ecriture seulement)
    mutable std::mutex                                        mutex_;
    std::condition_variable                                   wake_;
    std::deque<std::function<void()>>                         jobs_;
    bool                                                      stop_{false}, busy_{false};
    std::thread                                               thread_;
    std::deque<Frame>                                         frames_;      // le tampon circulaire (sous mutex_)
    std::size_t                                               frameBytes_{0};
    std::vector<std::pair<std::uint64_t, Frame>>              captures_;    // les images de defaut codees (sous mutex_)
    std::vector<std::string>                                  events_;      // (sous mutex_)
    std::vector<std::pair<std::string, hmi::incident::Snapshot>> createdQueue_;   // crees, a analyser (sous mutex_)
    std::vector<Retry>                                        pendingRetries_;      // a reessayer (sous mutex_)
    std::shared_ptr<const Listing>                            listing_;     // (sous mutex_)
    std::atomic<std::uint64_t>                                revision_{0};
};

} // namespace app
