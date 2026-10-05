// =============================================================================
//  app/ImportJob.hpp - 1.8.0 : un import .XPG / .XHW lu a cote, pas a pas
// -----------------------------------------------------------------------------
//  L'import d'un nouveau MAST (ou d'une configuration materielle) lit le
//  fichier, le programme (XML), l'analyse et le compare au projet ouvert : sur
//  un fil a part, pour que l'application continue a se dessiner et a repondre,
//  et qu'une fenetre montre l'avancee (ImportJobDialog), ou la barre du
//  haut quand on continue en arriere-plan.
//
//  LA REGLE QUI REND CELA SUR (ProjectImporter.hpp dit pourquoi l'ancien import
//  sur un fil a ete retire : le modele partage lu par l'image suivante). Ici le
//  fil ne touche JAMAIS au projet ouvert : il travaille sur un projet neuf qu'il
//  est seul a connaitre, et sur une COPIE du projet ouvert (prise avant, sur le
//  fil de l'interface) pour la comparaison. Quand il a fini, il ne touche plus a
//  rien ; le fil de l'interface voit done() (acquire/release) et reprend le
//  resultat. C'est lui, et lui seul, qui pose ensuite le programme dans le
//  projet (la commande, comme avant).
// =============================================================================
#pragma once

#include "../domain/ProjectModel.hpp"
#include "../project/MastImport.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace app {

class ImportJob {
public:
    enum class Kind : std::uint8_t { Mast, Hardware };

    // Ce que la fenetre montre (copie, sous verrou).
    struct State {
        int                 step{0};             // l'etape en cours ; steps().size() : fini
        std::vector<double> seconds;             // la duree de chaque etape finie (-1 : pas encore)
        std::string         detail;              // "Sections : 41 / 65 (SFC_PurgeB)"
        float               fraction{0.f};       // 0..1 : tout l'import
        double              elapsed{0.0};        // secondes
        std::size_t         sections{0}, units{0}, dfbs{0}, ddts{0}, variables{0};
        std::size_t         racks{0}, modules{0}, channels{0};
        bool                done{false}, failed{false}, cancelled{false};
        std::string         error;
    };
    struct StepInfo { std::string title, detail; };

    // `paths` : le .XPG (puis un .XHW) ou le .XHW seul. `current` : une copie du
    // projet ouvert, pour comparer ; `refs` : ce que l'IHM lit (le recapitulatif).
    ImportJob(Kind kind, std::vector<std::string> paths, std::shared_ptr<const domain::Project> current,
              std::vector<project::mast::HmiRef> refs, std::string source);
    ~ImportJob();
    ImportJob(const ImportJob&) = delete;
    ImportJob& operator=(const ImportJob&) = delete;

    void start();
    void cancel();
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] const std::vector<std::string>& paths() const noexcept { return paths_; }
    [[nodiscard]] const std::vector<StepInfo>& steps() const noexcept { return steps_; }
    [[nodiscard]] State state() const;
    [[nodiscard]] bool done() const noexcept { return done_.load(std::memory_order_acquire); }
    // Attendre la fin (les essais, la fermeture).
    void wait();

    // ---- le resultat : seulement quand done() (le fil est fini) ----
    std::shared_ptr<domain::Project>         imported;     // le programme (ou le materiel) lu
    std::optional<project::mast::Plan>       keepPlan, replacePlan;
    std::size_t                              hardwareChanges{0};   // .XHW seul : modules ajoutes, retires, changes

private:
    void run();
    void enter(int step, std::string detail);
    void leave(int step);
    void setDetail(std::string detail, float fraction);

    Kind                                   kind_;
    std::vector<std::string>               paths_;
    std::shared_ptr<const domain::Project> current_;
    std::vector<project::mast::HmiRef>     refs_;
    std::string                            source_;
    std::vector<StepInfo>                  steps_;
    std::vector<float>                     weights_;          // la part de chaque etape dans la barre
    mutable std::mutex                     lock_;
    State                                  state_;
    std::atomic_bool                       done_{false};
    std::atomic_bool                       cancel_{false};
    std::thread                            worker_;
    double                                 started_{0.0};
    double                                 stepStarted_{0.0};
};

} // namespace app
