// =============================================================================
//  app/ai/AiContext.hpp - l'Assistant IA : le contexte envoye avec une demande
// -----------------------------------------------------------------------------
//  LE CAIContextBuilder DE LA DEMANDE (4.4). Ce que le modele sait du projet au
//  moment de l'envoi, en texte compact (il le lit a chaque demande) :
//
//    - le projet (son nom) ;
//    - la vue active : son nom, sa taille, son fond, ses calques ; SES OBJETS
//      (nom, genre, x, y, w, h, et les proprietes qui ne sont PAS celles de la
//      creation par defaut, en abrege ; le groupe, le calque, le verrou) ;
//    - la selection (de gauche a droite) ;
//    - les variables de l'IHM (noms et types, la description courte ; JAMAIS
//      leurs valeurs en marche), les types qu'elles utilisent, et les variables
//      de l'automate relie (Env::plcVariables) ;
//    - 1.16.0 : les noms des types IHM du projet (une variable creee peut en
//      etre) ; les types (DDT) de l'automate relie, resumes (nom et membres),
//      quand la demande parle de types ou de l'automate, ou qu'il y en a peu ;
//    - les autres vues (leurs noms : une navigation les cite).
//
//  UNE GRANDE VUE SE RESUME. Le detail des objets a un budget (des octets) : la
//  selection d'abord, puis les objets les plus proches d'elle ; les autres ne
//  donnent que leur nom, ranges par genre, et le texte le dit au modele.
//
//  LA CONFIDENTIALITE (Settings) : sendView, sendSelection, sendVariables -
//  une partie decochee ne part pas, et le texte dit au modele qu'elle manque
//  (pour qu'il demande au lieu de deviner).
// =============================================================================
#pragma once

#include "AiPrompt.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace app::ai {

// Les budgets du contexte (octets).
struct ContextLimits {
    std::size_t objectBytes{9000};     // le detail des objets de la vue
    std::size_t nameBytes{3000};       // les noms des objets resumes
    std::size_t variableBytes{6000};   // les variables de l'IHM (les plus proches de la demande d'abord)
    std::size_t plcBytes{4000};        // les variables de l'automate relie
    std::size_t viewBytes{1500};       // les noms des autres vues
    // 1.16.0 (a la fin) : les types (DDT) de l'automate relie, resumes (leur nom, leurs membres).
    std::size_t plcTypeBytes{2500};
    // 1.18.0 (a la fin) : ce que les outils de l'IA lisent - les elements du projet (equipements, ressources, scripts,
    // fonctions, alarmes, dossiers), les variables systeme et les natives que la demande evoque.
    std::size_t toolBytes{3000};
    std::size_t sysBytes{2000};
    std::size_t nativeBytes{2500};
};

// Ce que le texte contient (les essais, le rapport).
struct ContextInfo {
    std::size_t objects{0}, detailed{0}, named{0};      // la vue : ses objets, detailles, nommes seulement
    std::size_t variables{0}, variablesListed{0};       // les variables de l'IHM
    std::size_t plc{0}, plcListed{0};                   // celles de l'automate relie
    bool        viewSent{false}, selectionSent{false}, variablesSent{false};
    bool        summarized{false};                      // une vue trop grande : resumee
    // 1.16.0 (a la fin) : les DDT de l'automate relie (combien, combien resumes) ; les types IHM du projet nommes.
    std::size_t plcTypes{0}, plcTypesListed{0}, hmiTypes{0};
};

// Le contexte de la demande (la fin du champ "system").
[[nodiscard]] std::string contextText(const PromptInput&, const ContextLimits& = {}, ContextInfo* info = nullptr);
// Les genres presents dans une vue (dans l'ordre de leur premier objet).
[[nodiscard]] std::vector<hmi::Kind> kindsInView(const hmi::View&);
// Les proprietes d'un objet qui different de la creation par defaut de son genre, en abrege
// ("label=Cuve 1 ; value=65 ; fill=fx(Etat_Pompe)") ; x, y, w, h n'y sont pas.
[[nodiscard]] std::string changedProps(const hmi::Object&, std::size_t maxValue = 40);

} // namespace app::ai
