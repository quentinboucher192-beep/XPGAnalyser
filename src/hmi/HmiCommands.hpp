// =============================================================================
//  hmi/HmiCommands.hpp — le document IHM et ses commandes annulables
// -----------------------------------------------------------------------------
//  TOUTE MODIFICATION PASSE PAR UNE COMMANDE, et toutes les commandes vont
//  dans la pile de l'application : Ctrl+Z reprend un deplacement d'objet comme
//  il reprend une creation de section, dans l'ordre ou on les a faits.
//
//  PAR INSTANTANE, PAS PAR OPERATION INVERSE. Une commande sait remettre la
//  vue d'avant et la vue d'apres ; annuler remet l'une, retablir l'autre.
//  Ecrire l'inverse de chacune des quarante operations de l'editeur (grouper,
//  distribuer, retourner un groupe tourne...) serait quarante occasions de se
//  tromper.
//
//  MAIS L'INSTANTANE NE GARDE QUE CE QUI CHANGE. Deux vues entieres par
//  commande, c'est quelques centaines de Ko pour deplacer un objet : la pile
//  ne pourrait pas etre longue. Une commande garde la vue sans ses objets
//  (calques, guides, scripts), l'ordre des objets avant et apres, et les SEULS
//  objets qui changent ; les autres sont repris de l'etat courant, qui est le
//  meme des deux cotes puisque la pile rejoue les commandes dans l'ordre.
//  Deplacer un objet coute deux objets, pas deux vues : la pile de
//  l'application peut alors etre profonde (100 000 pas).
//
//  LES GESTES CONTINUS SE FUSIONNENT : un glisser envoie une commande par image
//  avec la meme cle de fusion ("move:17"), et la pile n'en garde qu'une, qui
//  va de la position de depart a la position d'arrivee.
//
//  LES IDENTIFIANTS NE REVIENNENT JAMAIS. Annuler une creation ne rend pas son
//  identifiant : le compteur du projet ne recule pas, meme quand on remet un
//  instantane plus ancien.
// =============================================================================
#pragma once

#include "HmiHistory.hpp"
#include "HmiModel.hpp"
#include "HmiScenarios.hpp"      // lot 13 : les rapports des essais
#include "../core/Command.hpp"
#include "../core/Signal.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hmi {

class Document {
public:
    Project project;
    // Ce que la marche laisse (alarmes terminees, evenements, journal, mesures) :
    // pas une modification, donc hors de la pile d'annulation. Enregistre a
    // cote du projet (ihm/historique/).
    History history;
    // La vue touchee (kNoId : la structure du projet - vues, configuration).
    const core::SignalPtr<Id> changed = core::Signal<Id>::create();
    bool dirty{false};
    // Lot 13 : le dernier rapport de chaque essai (IHM > Essais) - de la marche,
    // pas du projet : ni annule, ni enregistre. `reported` : un rapport a change
    // (l'essai ; kNoId : tous).
    std::map<Id, ScenarioReport> reports;
    const core::SignalPtr<Id> reported = core::Signal<Id>::create();

    void touched(Id view) { dirty = true; changed->emit(view); }
};
using DocumentPtr = std::shared_ptr<Document>;

// Une liste d'elements a identifiant (les objets d'une vue, les vues d'un
// projet), gardee en difference : l'ordre avant et apres, et les seuls
// elements absents d'un cote ou differents.
template <class T>
struct ListDelta {
    std::vector<Id> orderBefore, orderAfter;
    std::vector<T>  before, after;
};

class ViewCommand final : public core::ICommand {
public:
    ViewCommand(DocumentPtr doc, View before, View after, std::string label, std::string mergeKey = {});
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override { return label_; }
    [[nodiscard]] bool mergeableWith(const core::ICommand&) const override;
    void mergeFrom(const core::ICommand&) override;
    [[nodiscard]] Id viewId() const noexcept { return viewId_; }
    // Les objets gardes par la commande, avant et apres confondus. Un
    // deplacement d'un objet en garde deux, quelle que soit la taille de la
    // vue : c'est ce que les tests verifient.
    [[nodiscard]] std::size_t storedObjects() const noexcept {
        return objects_.before.size() + objects_.after.size();
    }
    // Lot 19 : les objets touches, en clair, pour l'historique : leur nom s'il
    // n'y en a qu'un, "3 objets" sinon ; vide si aucun objet n'a change.
    [[nodiscard]] std::string objectsSummary() const;
private:
    core::Status put(bool after);
    DocumentPtr       doc_;
    Id                viewId_{kNoId};
    View              shellBefore_, shellAfter_;    // sans objets
    ListDelta<Object> objects_;
    std::string       label_, mergeKey_;
};

class ProjectCommand final : public core::ICommand {
public:
    ProjectCommand(DocumentPtr doc, Project before, Project after, std::string label, std::string mergeKey = {});
    core::Status execute() override;
    core::Status undo() override;
    [[nodiscard]] std::string label() const override { return label_; }
    // Une frappe dans un script general : une commande par touche, fusionnees
    // tant que la cle est la meme ("script:31") - Ctrl+Z reprend la saisie
    // entiere, pas une lettre.
    [[nodiscard]] bool mergeableWith(const core::ICommand&) const override;
    void mergeFrom(const core::ICommand&) override;
    [[nodiscard]] std::size_t storedViews() const noexcept {
        return views_.before.size() + views_.after.size();
    }
private:
    core::Status put(bool after);
    DocumentPtr     doc_;
    ListDelta<View> views_;
    // TOUT LE RESTE DU PROJET (configuration, ressources, fichiers externes,
    // scripts generaux, variables, et ce que les lots suivants ajoutent) : le
    // projet sans ses vues, garde en entier et seulement quand il change. Un
    // champ ajoute au projet est donc annulable sans rien ecrire ici.
    std::optional<Project> shellBefore_, shellAfter_;
    Id              nextId_{1};
    std::string     label_, mergeKey_;
};

// Fabrique une commande depuis une modification ecrite comme une fonction.
// Rend nullptr quand la fonction n'a rien change (ou que la vue n'existe
// pas) : l'appelant n'empile rien, et Ctrl+Z ne tombe pas sur un pas vide.
// La fonction recoit le projet reel pour pouvoir attribuer des identifiants.
using ViewChange    = std::function<void(Project&, View&)>;
using ProjectChange = std::function<void(Project&)>;

[[nodiscard]] core::CommandPtr changeView(const DocumentPtr&, Id view, std::string label,
                                          const ViewChange&, std::string mergeKey = {});
[[nodiscard]] core::CommandPtr changeProject(const DocumentPtr&, std::string label, const ProjectChange&,
                                             std::string mergeKey = {});

} // namespace hmi
