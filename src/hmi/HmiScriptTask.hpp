// =============================================================================
//  hmi/HmiScriptTask.hpp - 1.12.26 : un script detache, qui attend sans bloquer
// -----------------------------------------------------------------------------
//  L'interprete ST lit un programme en descendant dans son arbre : sa place est
//  une pile d'appels C++, il ne sait pas s'arreter au milieu d'une boucle pour
//  reprendre plus tard. Un script DETACHE tourne donc sur son propre fil - mais
//  jamais en meme temps que l'IHM : le fil de l'IHM lui passe la main (resume)
//  et attend qu'il la rende (yield : un WAIT, sa tranche de temps finie, sa
//  fin). Une seule chose tourne a la fois : le moteur n'a rien a proteger, le
//  passage de main (un verrou) suffit a ce que chacun voie ce que l'autre a
//  ecrit.
//
//  Annuler (cancel) : son yield en cours, a la reprise, et tous les suivants
//  rendent faux tout de suite - le script se deroule jusqu'a sa fin sans plus
//  rendre la main ; resume() attend cette fin.
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace hmi {

class ScriptTask {
public:
    using Body = std::function<void(ScriptTask&)>;
    explicit ScriptTask(Body body);
    ~ScriptTask();                                   // annule, deroule jusqu'a la fin, attend le fil
    ScriptTask(const ScriptTask&) = delete;
    ScriptTask& operator=(const ScriptTask&) = delete;

    // Le fil qui la lance (l'IHM) : la tache tourne jusqu'a ce qu'elle rende la main, ou finisse. Rien si elle est
    // finie. Le premier appel cree son fil.
    void resume();
    // Le fil de la tache : rendre la main a qui l'a lancee ; faux : la tache est annulee (le script s'arrete).
    [[nodiscard]] bool yield();
    void cancel() noexcept { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] bool finished() const;
    [[nodiscard]] bool started() const;

private:
    void run();

    Body                    body_;
    std::thread             thread_;
    mutable std::mutex      m_;
    std::condition_variable cv_;
    bool                    taskTurn_{false};        // vrai : la tache a la main
    bool                    started_{false}, finished_{false};
    std::atomic<bool>       cancelled_{false};
};

} // namespace hmi
