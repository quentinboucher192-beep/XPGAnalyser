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
//
//  1.13.0 (le debogueur des scripts IHM) : une tache par tick ou par geste des
//  qu'un point est arme. LES FILS SE REPRENNENT : une tache finie rend le sien a
//  une reserve, la suivante le reprend (pas un fil neuf par image) ; un fil sans
//  travail depuis 30 s s'en va. CE QUI TOUCHE L'ECRAN (les crochets du moteur : un
//  son, la Console, une question) passe par onRootThread : il tourne sur le fil
//  de l'IHM, la tache attend. UNE EXCEPTION qui sort du corps n'est plus avalee :
//  elle est gardee (takeFailure), le moteur la relance sur le fil de l'IHM (le
//  rapport de plantage, comme sans tache).
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace hmi {

class ScriptTask {
public:
    using Body = std::function<void(ScriptTask&)>;
    explicit ScriptTask(Body body);
    ~ScriptTask();                                   // annule, deroule jusqu'a la fin
    ScriptTask(const ScriptTask&) = delete;
    ScriptTask& operator=(const ScriptTask&) = delete;

    // Le fil qui la lance (l'IHM) : la tache tourne jusqu'a ce qu'elle rende la main, ou finisse. Rien si elle est
    // finie. Le premier appel lui donne un fil (repris de la reserve, ou neuf). Pendant ce temps, ce que la tache
    // demande au fil qui la lance (onRootThread) tourne ici.
    void resume();
    // Le fil de la tache : rendre la main a qui l'a lancee ; faux : la tache est annulee (le script s'arrete).
    [[nodiscard]] bool yield();
    void cancel() noexcept { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept { return cancelled_; }
    [[nodiscard]] bool finished() const;
    [[nodiscard]] bool started() const;
    // 1.13.0 : l'exception sortie du corps (nulle : aucune) - une fois : la prendre la retire.
    [[nodiscard]] std::exception_ptr takeFailure();

    // 1.13.0 : ce fil-ci est-il celui d'une tache ?
    [[nodiscard]] static bool onTaskThread() noexcept;
    // 1.13.0 : `fn` sur le fil de l'IHM (le premier qui a lance une tache) : depuis une tache, la main passe a qui
    // l'a lancee (et ainsi de suite, une tache lancee d'une tache), `fn` y tourne, puis la tache reprend la ou elle
    // etait. Hors d'une tache : tout de suite. Une exception de `fn` revient ici.
    static void onRootThread(const std::function<void()>& fn);

private:
    friend struct TaskWorkers;
    void run();                                      // le fil de la tache : le corps, puis la fin
    void callOwner(const std::function<void()>& fn); // le fil de la tache : `fn` chez qui l'a lancee

    Body                         body_;
    mutable std::mutex           m_;
    std::condition_variable      cv_;
    bool                         taskTurn_{false};   // vrai : la tache a la main
    bool                         started_{false}, finished_{false};
    std::atomic<bool>            cancelled_{false};
    const std::function<void()>* ownerCall_{nullptr}; // ce que la tache demande au fil qui la lance (nul : rien)
    std::exception_ptr           ownerFailure_;       // ... et son exception
    std::exception_ptr           failure_;            // l'exception sortie du corps
};

} // namespace hmi
