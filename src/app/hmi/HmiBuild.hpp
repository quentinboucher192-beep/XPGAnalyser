// =============================================================================
//  app/hmi/HmiBuild.hpp - 1.11.13 : le build de l'IHM dans l'application
// -----------------------------------------------------------------------------
//  LE GESTIONNAIRE DE BUILD (BuildManager de la specification) : un build a la
//  fois, dans un fil a part, sur une COPIE du projet - l'interface ne gele
//  jamais ; l'ecran lit la progression a chaque image (poll), annule, et recoit
//  le rapport (finished). Le moteur est hmi::pipeline (sans ecran) : analyse,
//  API, IHM, compilation, validation, cache, artefacts.
//
//  L'ANALYSE (les 13 etats de l'arbre, des editeurs, de la barre d'etat) est
//  refaite dans le meme fil quand le document change (invalidate), au plus une
//  fois toutes les 300 ms : un script modifie passe a « Modifie » aussitot, sans
//  rien construire. Elle relit le cache du disque (.xpg/build/build-cache.txt).
//
//  Un projet jamais enregistre (sans dossier) : le cache reste en memoire.
// =============================================================================
#pragma once

#include "../../core/Signal.hpp"
#include "../../hmi/HmiComm.hpp"
#include "../../hmi/HmiPipeline.hpp"
#include "../../ui/Theme.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace domain { struct Project; }

namespace app {

// ---- ce que montrent l'arbre et les editeurs ------------------------------------
// Un etat a l'ecran : son glyphe (un symbole que l'atlas dessine lui-meme) et son ton.
//   ✓ a jour / genere / compile     ● modifie, generation ou compilation requise
//   ◌ non genere                    ⚙ generation en cours      ◔ compilation en cours
//   ✕ generation ou compilation echouee                        ⛓ dependance invalide
//   ⏱ obsolete                      ⚠ a jour, avec des avertissements
struct HmiStateLook {
    std::string_view glyph;
    ui::Tone         tone{ui::Tone::None};
};
[[nodiscard]] HmiStateLook hmiStateLook(hmi::pipeline::State state, int warnings = 0);
// Le rang d'un etat pour resumer un dossier (le pire l'emporte).
[[nodiscard]] int hmiStateRank(hmi::pipeline::State state, int warnings = 0);

struct HmiBuildStatus {
    hmi::pipeline::Analysis analysis;
    hmi::pipeline::Cache    cache;
    std::unordered_map<std::string, hmi::pipeline::Shown> shown;   // cle -> etat montre
    std::unordered_map<hmi::Id, std::string> viewKeys;             // vue, popup, symbole, modele -> sa cle
    // Un dossier de l'arbre (un debut de chemin : "IHM", "IHM/Vues", "IHM/Vues/Vue_Accueil") :
    // le pire etat de ce qu'il contient, combien ne sont pas a jour, et pourquoi.
    struct Summary {
        hmi::pipeline::State state{hmi::pipeline::State::UpToDate};
        int         warnings{0};          // a jour avec des avertissements (le dossier le dit)
        std::size_t elements{0}, pending{0}, failed{0};
        std::string tip;
    };
    std::unordered_map<std::string, Summary> folders;
    struct Totals {
        std::size_t elements{0}, upToDate{0}, modified{0}, notGenerated{0}, required{0}, toGenerate{0}, toCompile{0}, failed{0}, invalid{0}, obsolete{0};
        int         errors{0}, warnings{0};
    } totals;
    std::uint64_t generation{0};
    [[nodiscard]] const hmi::pipeline::Shown* of(std::string_view key) const;
    [[nodiscard]] const hmi::pipeline::Shown* ofView(hmi::Id view) const;
    [[nodiscard]] const Summary*              folder(std::string_view path) const;
    [[nodiscard]] std::string                 keyOfView(hmi::Id view) const;
    [[nodiscard]] bool upToDate() const noexcept { return totals.elements > 0 && totals.upToDate == totals.elements && analysis.deleted.empty(); }
    // "291 elements a jour" / "3 modifies, 1 en erreur" : la barre d'etat
    [[nodiscard]] std::string headline() const;
};
// L'analyse mise en forme (les etats, les dossiers, les totaux) : le fil du build s'en sert.
[[nodiscard]] std::shared_ptr<HmiBuildStatus> hmiBuildStatusOf(hmi::pipeline::Analysis analysis, hmi::pipeline::Cache cache);

// ---- ce qu'il faut pour construire, pris sur le fil de l'interface --------------
struct HmiBuildSetup {
    std::shared_ptr<const hmi::Project> project;   // une copie
    hmi::pipeline::ApiInfo  api;
    std::string             projectFolder;          // vide : jamais enregistre (le cache reste en memoire)
    hmi::NameExists         plcHasName;
    hmi::exprcheck::PlcPaths plcPaths;
    std::optional<hmi::comm::Plan> plan;            // le plan Modbus (la validation)
};
// L'API telle que l'IHM la voit : les variables globales (nom, type, adresse) et
// les types derives (leurs champs) du projet automate.
[[nodiscard]] hmi::pipeline::ApiInfo hmiApiInfo(const domain::Project* plc);

class HmiBuildManager {
public:
    using SetupFn = std::function<std::optional<HmiBuildSetup>()>;
    explicit HmiBuildManager(SetupFn setup);
    ~HmiBuildManager();
    HmiBuildManager(const HmiBuildManager&) = delete;
    HmiBuildManager& operator=(const HmiBuildManager&) = delete;

    // Le document a change : l'analyse sera refaite (au prochain poll, 300 ms apres).
    void invalidate();
    // L'analyse tout de suite (au prochain poll, sans attendre).
    void analyseNow();
    // Lancer un build. Faux (et pourquoi) : un build tourne deja, ou rien a construire.
    bool start(const hmi::pipeline::Request& request, std::string* why = nullptr);
    [[nodiscard]] bool building() const noexcept { return building_; }
    [[nodiscard]] bool busy() const noexcept { return worker_.joinable(); }
    void cancel();
    [[nodiscard]] bool cancelling() const noexcept { return cancel_.load(); }
    // A chaque image (fil de l'interface) : ce qui est fini est applique, les
    // signaux partent. Vrai : quelque chose a change (progression, etat, rapport).
    bool poll();
    // Attendre la fin du travail en cours (les essais, la fermeture).
    void wait();

    [[nodiscard]] std::shared_ptr<const HmiBuildStatus> status() const noexcept { return status_; }
    [[nodiscard]] hmi::pipeline::Progress progress() const;
    [[nodiscard]] double elapsed() const;    // secondes depuis le debut du build en cours (ou du dernier)
    [[nodiscard]] std::shared_ptr<const hmi::pipeline::Report> lastReport() const noexcept { return report_; }
    [[nodiscard]] const hmi::pipeline::Request& lastRequest() const noexcept { return request_; }

    const core::SignalPtr<> statusChanged = core::Signal<>::create();
    const core::SignalPtr<std::shared_ptr<const hmi::pipeline::Report>> finished =
        core::Signal<std::shared_ptr<const hmi::pipeline::Report>>::create();

private:
    struct Done {
        std::shared_ptr<const hmi::pipeline::Report> report;   // nul : une analyse
        std::shared_ptr<HmiBuildStatus>              status;
        std::optional<hmi::pipeline::Cache>          memCache;  // le cache en memoire (projet sans dossier)
    };
    void launch(HmiBuildSetup setup, std::optional<hmi::pipeline::Request> request);
    void join();

    SetupFn                 setup_;
    std::thread             worker_;
    std::atomic<bool>       cancel_{false};
    std::atomic<bool>       finishedFlag_{false};
    bool                    building_{false};
    bool                    dirty_{true};
    std::chrono::steady_clock::time_point dirtyAt_{};
    std::chrono::steady_clock::time_point startedAt_{}, endedAt_{};
    mutable std::mutex      mutex_;
    hmi::pipeline::Progress progress_;
    bool                    progressSeen_{true};
    std::optional<Done>     done_;
    std::shared_ptr<const HmiBuildStatus>        status_{std::make_shared<HmiBuildStatus>()};
    std::shared_ptr<const hmi::pipeline::Report> report_;
    hmi::pipeline::Request  request_;
    std::optional<hmi::pipeline::Cache> memCache_;   // projet sans dossier
};

} // namespace app
