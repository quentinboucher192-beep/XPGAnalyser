// =============================================================================
//  app/ai/AiIncidentAnswers.hpp - 1.20.0 : l'Assistant IA DIAGNOSTIQUE LES INCIDENTS
// -----------------------------------------------------------------------------
//  Les questions sur les dossiers d'incident (AiIncident.hpp), repondues SANS
//  RESEAU depuis ce qu'ils contiennent - le contexte copie au moment du defaut
//  (la chaine de dependances, les valeurs, la chronologie, les alarmes liees,
//  les pistes classees) - et, s'il est donne, le projet ouvert (le graphe des
//  relations, hmi::rel). Comme AiProjectAnswers : JAMAIS une variable, un
//  equipement ou une relation inventes ; une valeur absente du dossier est dite
//  << non disponible >> ; l'ordre d'apparition n'est jamais une cause.
//
//    << Pourquoi cette alarme est-elle apparue ? >>          les pistes classees
//    << Fais-moi un diagnostic complet de cet incident >>    la fiche (10 sections)
//    << Retrouve toute la chaine de dependances... >>        la chaine, par profondeur
//    << Quelles variables peuvent etre a l'origine... >>     directes et indirectes, leurs valeurs
//    << Pourquoi cinq alarmes sont-elles apparues en meme temps ? >>   les simultanees (1 s),
//                                                            les dependances et equipements communs
//    << Est-ce que cette alarme est deja apparue... >>       ses incidents, l'historique
//    << Compare les trois derniers incidents de cette pompe >>
//    << Quel equipement est commun a ces differents defauts ? >>
//    << Montre-moi les scripts qui peuvent influencer cette variable >>
//    << Est-ce que les defauts de communication expliquent les alarmes de mesure ? >>
//                                                            << peut expliquer >> (indirect), jamais << a cause >>
//
//  Le sujet (<< cette alarme >>, << la pompe 2 >>, Pompe_2.Pression) est lu dans
//  la demande et cherche dans les dossiers (sans casse ni accents) ; sans sujet :
//  le dernier incident. Les elements cites sont nommes en `code` (le panneau ne
//  rend pas de lien dans le texte) et rendus dans Answer::refs, dans le
//  vocabulaire de Assistant::Host::reveal, pour qui veut les ouvrir.
//
//  Fonctions PURES : rien n'est lu du disque ni ecrit ; l'hote donne la liste.
// =============================================================================
#pragma once

#include "AiIncident.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::diagnosis {

enum class Question : std::uint8_t {
    None,              // pas une question sur les incidents
    Why,               // pourquoi cette alarme est apparue
    Diagnosis,         // le diagnostic complet (la fiche)
    Chain,             // la chaine de dependances
    Origins,           // les variables qui peuvent etre a l'origine
    Simultaneous,      // des alarmes apparues en meme temps
    History,           // deja apparue dans le passe
    Compare,           // comparer les N derniers incidents d'un element
    CommonEquipment,   // l'equipement commun a des defauts
    Scripts,           // les scripts qui peuvent influencer une variable
    Communication,     // les defauts de communication expliquent-ils les alarmes de mesure
    List,              // la liste des incidents
};

// Un element cite par la reponse (un lien de navigation possible) : `kind` dans le vocabulaire de Host::reveal
// ("alarme", "variable", "equipement", "script", "fonction", "vue", "objet", "type") ; `view` : la vue d'un objet.
struct Ref {
    std::string kind, name, view;
};

struct Answer {
    std::string              text;            // markdown simple, au tutoiement
    std::vector<std::string> suggestions;     // les questions suivantes
    std::vector<Ref>         refs;            // les elements cites (sans doublon)
    Question                 question{Question::None};
    bool                     understood{true};
};

// La sorte de question (None : pas une question sur les incidents). Une demande de creation (ajoute, cree, anime...)
// sans le mot incident ni diagnostic n'en est jamais une : << Ajoute une alarme de defaut >> reste a l'IA integree.
[[nodiscard]] Question classify(std::string_view request);
[[nodiscard]] bool     isIncidentQuestion(std::string_view request);

// La reponse, depuis `items` (en attente et valides, dans n'importe quel ordre ; les illisibles sont ignores) et le
// projet ouvert (nul : les dossiers seuls).
[[nodiscard]] Answer answer(std::string_view request, const std::vector<incidents::Item>& items, const hmi::Project* project = nullptr);

// Pour l'IA distante : les `count` derniers incidents que la demande concerne, resumes (date, alarme, resume, valeurs,
// pistes et leur certitude, equipements, alarmes liees) - jamais tout le projet ni toute la fiche ; borne a `maxBytes`.
[[nodiscard]] std::string contextBlock(std::string_view request, const std::vector<incidents::Item>& items, std::size_t maxBytes = 6000,
                                       std::size_t count = 3);

} // namespace app::ai::diagnosis
