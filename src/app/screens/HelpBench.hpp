// =============================================================================
//  app/screens/HelpBench.hpp — le banc d'essai de l'aide, comme un instrument
// -----------------------------------------------------------------------------
//  CE QUE L'ONGLET ESSAI DOIT DONNER A VOIR, EN UNE SECONDE.
//
//    EST-CE QUE CA TOURNE ?  La transport, comme un lecteur : un gros bouton
//        rond Lecture / Pause, Arret, Un cycle, une LED qui respire tant que ca
//        tourne, et le compteur de cycles en grand.
//
//    QU'EST-CE QUI BOUGE ?  La table d'animation : une LED par booleen, une
//        jauge par nombre, un eclair sur la valeur qui vient de changer, et les
//        lignes forcees en ambre avec un cadenas - forcer est le seul geste qui
//        fausse ce qu'on regarde, il ne doit jamais passer inapercu.
//
//    COMMENT CA S'ENCHAINE ?  Le chronogramme, a la maniere d'un analyseur
//        logique : un couloir par signal, les booleens en creneaux, les nombres
//        en paliers, une grille par dizaine de cycles, et un curseur qui suit la
//        souris et lit toutes les valeurs a ce cycle-la.
//
//    ET LE BLOC LUI-MEME ?  Dessine en FBD, comme dans Control Expert : ses
//        entrees a gauche, ses sorties a droite, ce qui est branche au bout de
//        chaque fil, et ce qui y passe - un fil booleen s'allume quand il vaut
//        TRUE. Un clic sur une entree booleenne la bascule.
//
//  Le calcul reste dans help::TryBench, qui se teste sans ecran ; ce fichier ne
//  fait que le montrer. Les classes de dessin vivent dans le .cpp : la page ne
//  voit que BenchPane.
// =============================================================================
#pragma once

#include "../../help/TryBench.hpp"
#include "../../project/LibraryCatalog.hpp"
#include "../../ui/Layout.hpp"
#include "../../ui/widgets/Containers.hpp"

#include <memory>
#include <string>

namespace app {

class BenchWatchList;
class BenchTransport;
class BenchChronogram;
class BenchFbd;

class BenchPane final : public ui::DockLayout {
public:
    explicit BenchPane(std::string id);
    ~BenchPane() override;

    // Le banc, et l'entree dont il fait tourner l'exemple (pour dessiner le
    // bloc). nullptr : plus d'essai, l'onglet montre comment en lancer un.
    void setBench(std::shared_ptr<help::TryBench> bench, const project::CatalogEntry* entry);
    [[nodiscard]] help::TryBench* bench() const noexcept { return bench_.get(); }

    // Le temps de l'image, venu de l'ecran : un widget n'a pas de pouls a lui.
    void tick(double deltaSeconds);
    void refresh();

    // Fermer l'essai (le bouton, ou Echap dans l'onglet).
    const core::SignalPtr<> closed = core::Signal<>::create();
    // L'etat a change : l'onglet allume ou eteint sa LED.
    const core::SignalPtr<help::TryBench::State> stateChanged =
        core::Signal<help::TryBench::State>::create();
    // Un message pour le bandeau de la page (forcage, erreur).
    const core::SignalPtr<const std::string&, bool> message =
        core::Signal<const std::string&, bool>::create();

    // --- seams de test ------------------------------------------------------
    void transportForTest(int command);          // 1 lecture/pause, 2 arret, 3 un cycle
    void periodForTest(int ms);
    bool clickLedForTest(const std::string& name);
    bool clickTraceForTest(const std::string& name);
    bool clickLockForTest(const std::string& name);
    bool clickPinForTest(const std::string& pin);
    void filterForTest(std::string term);
    [[nodiscard]] std::size_t visibleRowsForTest() const;
    [[nodiscard]] std::string instanceForTest() const;
    [[nodiscard]] std::size_t pinCountForTest() const;

protected:
    ui::EventResult onEvent(const ui::InputEvent&) override;

private:
    void autoTrace();

    std::shared_ptr<help::TryBench> bench_;
    const project::CatalogEntry*    entry_{nullptr};
    help::TryBench::State           lastState_{help::TryBench::State::Stopped};

    BenchTransport*   transport_{nullptr};
    BenchWatchList*   watch_{nullptr};
    BenchChronogram*  chrono_{nullptr};
    BenchFbd*         fbd_{nullptr};
    ui::StatusBar*    status_{nullptr};
    core::ConnectionScope links_;
};

} // namespace app
