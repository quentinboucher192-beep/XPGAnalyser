// =============================================================================
//  app/ai/AiDesign.hpp - 1.19.0 : l'IA qui CONCOIT et CONSTRUIT des projets complets
// -----------------------------------------------------------------------------
//  La bibliotheque de conception de l'IA integree (sans connexion, a regles). Une
//  demande de haut niveau (<< cree une station de pompage complete avec deux
//  pompes, une cuve, les equipements Modbus, les variables, les alarmes, les
//  scripts, les symboles, les popups et la vue de supervision >>) devient UNE
//  proposition (AiOperations.hpp) : des dizaines d'operations, ecrites avec les
//  seules operations du contrat - rien d'invente :
//
//    les types        T_Pompe, T_Cuve... (creer_type) ; un type du meme nom deja
//                     dans le projet est REUTILISE tel quel (jamais remplace) et
//                     l'installation suit SES membres (les roles : Marche,
//                     Defaut, Mode_Auto ou Auto...) ;
//    les variables    Pompe_1..N, Cuve_1..M (creer_variables) ; une variable
//                     identique sert telle quelle ; un nom pris par une autre
//                     variable : le numero libre suivant, et le texte le dit ;
//    l'equipement     Modbus TCP SIMULE (la station marche sans reseau ; l'adresse
//                     IP se regle ensuite) et les structures a des adresses
//                     libres consecutives (hmi::audit::freeRanges, l'emprise de
//                     chaque type mesuree par hmi::audit::ranges) ;
//    les alarmes      defaut de pompe, niveaux haut et bas de cuve... ;
//    les scripts      ST, au demarrage et cyclique - COMPILES par la validation,
//                     jamais executes ici ;
//    le symbole       (role symbole, un parametre du type) et la POPUP (role
//                     popup) : des objets lies au parametre par des formules ;
//    la vue           le synoptique : les objets du synoptique lies aux
//                     variables, les tuyauteries, un titre, un bandeau d'alarme,
//                     un bouton << Details >> par element (ouvrir la popup, son
//                     parametre passe : Pompe := Pompe_1) ;
//    les dossiers     la vue, le symbole, la popup et les scripts dans le dossier
//                     de l'installation ; les variables neuves rangees.
//
//  MODIFIER L'EXISTANT : etendre une serie (une troisieme pompe << sur le meme
//  modele >> : sa variable, son adresse, ses alarmes, ses objets dans chaque vue
//  qui montre le modele), dupliquer un element, une alarme sur toute une serie,
//  remplacer le symbole d'instances, ajouter une mesure, ranger dans un dossier.
//  Chaque texte dit COMBIEN d'elements sont concernes avant que rien ne change.
//
//  Le texte dit aussi, honnetement, ce que les operations ne savent pas faire
//  (Design::limits) : poser une instance de symbole, relier une commande d'une
//  popup a son parametre, reecrire un script existant.
//
//  Rien n'est applique ici : la proposition passe par app::ai::validate (sur une
//  copie) puis makeCommand (UN Ctrl+Z). Proposal::request est vide : l'appelant y
//  met la demande (les descriptions creees la citent).
// =============================================================================
#pragma once

#include "AiOperations.hpp"
#include "../../hmi/HmiModel.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::design {

// ---- les installations completes ------------------------------------------------
enum class Installation : std::uint8_t { PumpingStation, ProductionLine, TankFarm, Regulation };
// "station de pompage", "ligne de production", "parc de cuves", "r\xC3\xA9gulation" (accents compris).
[[nodiscard]] std::string_view installationLabel(Installation) noexcept;

struct InstallationSpec {
    Installation kind{Installation::PumpingStation};
    int          pumps{2};    // station : pompes ; ligne : convoyeurs ; regulation : vannes reglantes
    int          tanks{1};    // station, parc de cuves : cuves
    int          loops{1};    // regulation : boucles PID
    std::string  name;        // le nom de base (vide : Station, Ligne, Parc_Cuves, Regulation) ; donne, il prefixe
                              // aussi les variables (Nord_Pompe_1)
    bool equipment{true}, alarms{true}, scripts{true}, symbols{true}, popups{true}, view{true};
};

// Une conception : la proposition, le texte de la reponse, ce qui est reutilise, ce qui n'a pas pu etre fait.
struct Design {
    Proposal                 proposal;   // ses operations (vides : rien a faire, le texte dit pourquoi) ; portee et empreinte faites
    std::string              text;       // markdown leger : combien de chaque, reutilise, a savoir
    std::vector<std::string> reused;     // ce qui existe deja et sert tel quel ("Type T_Pompe (7 membres)")
    std::vector<std::string> limits;     // ce que les operations ne savent pas exprimer, ou ce qui est laisse de cote
};

// Une installation complete, construite sur le projet tel qu'il est (ce qui existe est reutilise).
[[nodiscard]] Design installation(const hmi::Project&, const InstallationSpec&);

// ---- modifier l'existant --------------------------------------------------------------
// Une SERIE : les variables Prefixe_1, Prefixe_2... (sans casse ; "pompe", "pompes", "Pompe_" : Pompe_N).
// << Ajoute une troisieme pompe sur le meme modele >> : `count` elements de plus, copies du dernier de la serie -
// la variable (meme type), son adresse (meme equipement, une place libre de meme emprise), ses alarmes, ses objets
// dans chaque vue qui le montre (decales du pas de la serie).
[[nodiscard]] Design extendSeries(const hmi::Project&, std::string_view prefix, int count);
// << Duplique la cuve 1 en cuve 4 >> : `from`, `to` - un nom de variable ou "cuve 1" (Cuve_1).
[[nodiscard]] Design duplicateItem(const hmi::Project&, std::string_view from, std::string_view to);
// << Ajoute une alarme de temperature haute sur les quatre cuves >> : sur chaque element de la serie, la condition
// "Element.membre comparaison seuil" (`member` vide : l'element lui-meme ; `comparison` : >, >=, <, <=, =, <> ou
// haut / bas ; vide : un membre BOOL). `label` : le nom de l'alarme apres celui de l'element ("Temperature_Haute").
[[nodiscard]] Design alarmOnSeries(const hmi::Project&, std::string_view prefix, std::string_view member, std::string_view comparison,
                                   std::string_view threshold, std::string_view label);
// << Remplace le symbole de toutes les pompes par X >> : les INSTANCES de symbole de la serie (leur nom, leur symbole
// d'aujourd'hui ou leurs parametres la citent) prennent le symbole `newSymbol` (leurs parametres suivent).
[[nodiscard]] Design replaceSymbol(const hmi::Project&, std::string_view prefix, std::string_view newSymbol);
// << Ajoute une mesure de pression sur la pompe 2 >> : `on` (Pompe_2, "pompe 2"), `what` (pression, temperature, debit,
// niveau, courant, vitesse) - la variable de mesure (ou le membre du type s'il existe), son adresse, un instrument
// pres de l'element dans chaque vue qui le montre.
[[nodiscard]] Design addMeasure(const hmi::Project&, std::string_view on, std::string_view what);
// << Deplace toutes les vues de production dans le dossier Production >> : `listKey` (vues, popups, symboles,
// scripts, types, variables...), `filter` (dans le nom, le dossier ou la description ; sans casse ni accents ; vide :
// tous), `folder` (le chemin du dossier).
[[nodiscard]] Design moveToFolder(const hmi::Project&, std::string_view listKey, std::string_view filter, std::string_view folder);

} // namespace app::ai::design
