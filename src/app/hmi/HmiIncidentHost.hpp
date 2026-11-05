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
//
//  1.21.0 : UNE ALARME QUI BAT (la meme alarme toutes les X secondes, des jours
//  durant) ne fait plus des centaines de milliers d'incidents. LA REGLE : une
//  apparition d'une alarme dont l'incident a ete cree, ou qui a reapparu ou
//  disparu, il y a moins de Settings::regroup secondes (600 par defaut ; la
//  fenetre GLISSE a chaque activite ; elle ne court pas tant que l'alarme est
//  active) est une REAPPARITION de cet incident : aucun dossier, aucune image,
//  aucune video - elle est comptee dans sa rafale (hmi::incident::Burst, bornee :
//  quelques Ko quel que soit le nombre). Seule une alarme restee calme plus
//  longtemps que la fenetre rouvre un vrai incident. Un incident VALIDE (ou
//  supprime) est clos : la reapparition suivante ouvre un nouvel incident (la
//  copie validee est verifiee et figee ; et << ca recommence apres ma
//  validation >> est une information a voir). Sur le disque : reapparitions.json
//  dans le dossier (hors empreinte), reecrit sur le fil d'ecriture au plus toutes
//  les 5 s pendant que l'alarme bat, a sa fin et a l'arret - jamais une ecriture
//  par reapparition. Les defauts de communication qui battent : de meme.
//  DEUX GARDE-FOUS GLOBAUX : au plus Settings::maxPerHour NOUVEAUX incidents par
//  heure glissante (au-dela : comptes, sans dossier, dit au journal et dans le
//  volet) ; au-dela de Settings::mediaQuotaMb Mo d'images et de videos (la somme
//  des tailles relues avec la liste, jamais un parcours du disque a part), les
//  nouveaux incidents sont crees SANS image ni video (le texte, la chronologie et
//  la fiche restent ; c'est dit). Rien n'est jamais supprime automatiquement.
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
#include <set>
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
    // 1.25.1 : supprimer, c'est mettre dans la corbeille des incidents (en attente/.corbeille) ; rend ou il ira (vide :
    // pas de stockage). restore l'en ressort, a sa place (Ctrl+Z). Les deux en arriere-plan, comme le reste.
    std::string remove(const std::string& folder);
    void restore(const std::string& slot, const std::string& folder);
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

    // ---- 1.21.0 : les reapparitions regroupees, les garde-fous (fil de l'interface) ----
    // La rafale EN COURS de l'incident `id` (son alarme bat, ou sa fenetre court encore) ; nulle : aucune - le volet lit
    // alors celle du dossier (Item::reappearances, relue avec la liste).
    [[nodiscard]] const hmi::incident::Burst* reappearances(const std::string& id) const;
    [[nodiscard]] std::size_t liveGroups() const noexcept { return groups_.size(); }   // les rafales en memoire (une par alarme au plus)
    // Change a chaque reapparition comptee, a chaque incident refuse par le plafond : le volet se refait (au plus par seconde).
    [[nodiscard]] std::uint64_t liveRevision() const noexcept { return liveRevision_; }
    struct Guards {
        std::size_t   maxPerHour{0};          // le plafond (0 : aucun)
        std::size_t   createdLastHour{0};     // les nouveaux incidents de l'heure glissante
        bool          capReached{false};      // le plafond atteint en ce moment
        std::uint64_t skipped{0};             // les incidents non crees (plafond horaire), depuis le lancement
        std::uint64_t skippedNow{0};          // ... depuis que le plafond est atteint (0 : il ne l'est pas)
        std::string   skippedSince;           // l'horodatage du premier refus de l'episode en cours
        std::vector<std::pair<std::string, std::uint64_t>> skippedBy;   // l'episode en cours, par alarme (les plus touchees d'abord)
        std::uint64_t mediaBytes{0}, quotaBytes{0};   // les images et videos des incidents ; le quota (0 : aucun)
        bool          quotaReached{false};
        std::uint64_t withoutMedia{0};        // les incidents crees sans image ni video (le quota), depuis le lancement
    };
    [[nodiscard]] Guards guards() const;

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
        std::string          id;             // 1.21.0 : l'incident (connu des le front : sa rafale s'ouvre aussitot)
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
        std::string             extra;      // 1.21.0 : le resume des reapparitions (le contexte de l'analyse)
    };
    struct Group {                           // 1.21.0 : les reapparitions regroupees d'une alarme (fil de l'interface)
        std::string          id;             // l'incident
        std::string          alarm;          // le nom tel qu'ecrit
        std::string          valuePath;      // la premiere variable de la condition : lue a chaque reapparition
        double               last{0};        // la derniere activite (creation, reapparition, disparition)
        double               written{-1e9};  // la derniere ecriture de reapparitions.json demandee
        bool                 active{true};   // l'alarme est active : la fenetre ne court pas
        bool                 dirty{false};   // des reapparitions pas encore ecrites
        hmi::incident::Burst burst;
    };
    struct Retry {                           // un incident a ecrire (garde en memoire si le disque refuse)
        hmi::incident::Snapshot          snapshot;
        app::ai::incidents::Capture      capture;
        int                              attempts{0};
        std::size_t                      maxPending{200};   // (copie du reglage : le fil d'ecriture ne lit pas settings_)
    };
    void post(std::function<void()> job);
    void worker();
    // 1.21.0 : les fronts du detecteur - les reapparitions regroupees, puis la case Incidents (`n` : l'avis d'alarme ;
    // nul : un defaut de communication) et le plafond horaire ; les fronts qui restent sont des incidents a creer.
    void route(const std::vector<hmi::incident::Event>& fresh, std::vector<hmi::incident::Event> fronts, const hmi::AlarmNotice* n);
    void startGroup(const hmi::incident::Event& e, const std::string& id);
    void flushGroup(Group& g, bool relistAfter);   // reapparitions.json, sur le fil d'ecriture (rien si rien de neuf)
    void closeGroups();                            // tout ecrire, tout oublier (l'arret, un autre projet)
    bool admit(const hmi::incident::Event& e);     // le plafond horaire : faux, compte sans dossier
    [[nodiscard]] bool overQuota() const;           // le quota des images et videos depasse
    [[nodiscard]] std::string idOf(const std::string& folder) const;   // (la liste)
    void writeBurst(const std::shared_ptr<app::ai::incidents::Store>& store, const std::string& id, const hmi::incident::Burst& b,
                    bool relistAfter);                                   // (fil d'ecriture)
    void forget(const std::string& id);            // (fil d'ecriture) valide, supprime, abandonne : sa rafale est close
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
    // 1.21.0 : les rafales (par alarme en majuscules ; "COMM:" + equipement) et les garde-fous (fil de l'interface)
    std::map<std::string, Group>                              groups_;
    std::uint64_t                                             liveRevision_{0};
    std::deque<double>                                        createdTimes_;   // les nouveaux incidents de l'heure glissante
    std::uint64_t                                             capSkipped_{0}, capEpisode_{0};
    std::string                                               capSince_;
    std::map<std::string, std::uint64_t>                      capBy_;          // (borne)
    bool                                                      quotaSaid_{false};
    std::uint64_t                                             withoutMedia_{0};
    std::atomic<std::uint64_t>                                mediaBytes_{0};  // (ecrit par le fil d'ecriture)
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
    std::map<std::string, hmi::incident::Burst>               waitingBursts_;   // 1.21.0 (fil d'ecriture) : l'incident pas encore ecrit
    std::set<std::string>                                     forgotten_, burstFailed_;   // 1.21.0 (fil d'ecriture)
    std::vector<std::string>                                  forgetQueue_;     // 1.21.0 (sous mutex_) : vers le fil de l'interface
    struct BurstOut {                                                           // 1.21.0 : une ecriture de reapparitions.json en attente
        hmi::incident::Burst burst;
        bool                 relist{false};
    };
    std::map<std::string, BurstOut>                           burstOut_;        // 1.21.0 (sous mutex_) : une seule par incident
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
